# ojay

A compiler for the Jai programming language, written from publicly available
information about the language. It has its own frontend (lexer, parser, type
checker) and uses LLVM as the backend.

Each target architecture has its own compiler executable:

| Executable    | Target                                   | Output          |
|---------------|------------------------------------------|-----------------|
| `ojay-x86_64` | x86-64 Linux                             | ELF object file |
| `ojay-arm32`  | ARM Cortex-M33 (ARMv8-M Mainline, FPU), bare metal | ELF object file |
| `ojay-riscv`  | RISC-V RV32IMAC (ESP32-C6), bare metal   | ELF object file |

## Building

Requirements: CMake ≥ 3.20, a C++20 compiler, LLVM (developed against 23), and
`clang` (cross-compiles the runtime). Linking programs (and the tests) needs
`cc` and `ld.lld` for x86-64, `arm-none-eabi-gcc` for ARM, and
`riscv32-esp-elf-gcc` (from ESP-IDF's tools) for RISC-V.

```sh
cmake -S . -B build -G Ninja
ninja -C build
python3 tests/run_tests.py      # or: ctest --test-dir build
```

ARM32 tests run on an attached FRDM-MCXN947 when one is connected, otherwise
under `qemu-system-arm` if installed, otherwise they are only built
(`--arm=board|board-flash|qemu|none` to choose; `board-flash` programs every
test into flash). OpenOCD is needed for the board, LinkServer for flash.

The build produces `build/bin/ojay-*`, plus the runtime libraries and board
linker scripts in `build/lib/ojay/`.

## Usage

The compilers only compile: each run turns one Jai program (plus whatever it
`#load`s / `#import`s) into one object file. Linking is done with the
platform's normal linker against the ojay runtime library, and flashing or
running the result is left to the usual tools.

```sh
build/bin/ojay-x86_64 -O2 hello.jai          # -> hello.o
build/bin/ojay-arm32  -O2 hello.jai -o hello.arm32.o
build/bin/ojay-riscv  -O2 hello.jai -o hello.riscv.o
```

Options: `-o`, `-c` (object, the default), `-S` (assembly), `--emit-llvm`,
`-O0..-O3`, `--target`, `--cpu`, `--features`, `--pic`/`--no-pic`, `-I`,
`--no-bounds-check`. See `--help`. If the program defines `main`, the object
also defines the C entry point `main`; otherwise it is a library object.
`#system_library` dependencies are recorded in the object's `.deplibs`
section, which `ld.lld` honors; with other linkers pass them as `-l` options.

### Linking

`build/lib/ojay/` (or `<prefix>/lib/ojay/` after `ninja install`) holds the
runtime libraries and the board linker scripts. `examples/Makefile` shows
the complete flow.

x86-64 Linux, with libc:

```sh
cc hello.o $OJAY/lib/ojay/libojayrt_x86_64_hosted.a -lm -o hello
```

x86-64 Linux, static without libc (compile with `--no-pic`):

```sh
ld.lld -static --gc-sections hello.o $OJAY/lib/ojay/libojayrt_x86_64_freestanding.a -o hello
```

ARM Cortex-M33 (`arm-none-eabi-gcc` supplies libgcc, which handles 64-bit
division and `float64`, and newlib's libm):

```sh
arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16 \
    -nostdlib -nostartfiles -Wl,--gc-sections \
    -L$OJAY/lib/ojay/boards -T mcxn947-flash.ld \
    hello.arm32.o $OJAY/lib/ojay/libojayrt_arm32_baremetal.a \
    -Wl,--start-group -lm -lc_nano -lgcc -Wl,--end-group -o hello.elf
```

RISC-V ESP32-C6 (`riscv32-esp-elf-gcc` supplies libgcc, which handles
floating point and 64-bit division, and newlib's libm):

```sh
riscv32-esp-elf-gcc -march=rv32imac_zicsr_zifencei -mabi=ilp32 \
    -nostdlib -nostartfiles -Wl,--gc-sections \
    -L$OJAY/lib/ojay/boards -T esp32c6.ld \
    hello.riscv.o $OJAY/lib/ojay/libojayrt_riscv32_esp32c6.a \
    -Wl,--start-group -lm -lc_nano -lgcc -Wl,--end-group -o hello-esp32c6.elf
```

Board linker scripts in `runtime/boards/`:

| Script              | Layout                                                        |
|---------------------|---------------------------------------------------------------|
| `mcxn947-flash.ld`  | NXP MCX N947 core 0: code in flash at 0x0, data in SRAM        |
| `mcxn947-ram.ld`    | MCX N947: everything in SRAM, for loading with a debugger      |
| `mps2-an505.ld`     | Arm MPS2+ AN505 (Cortex-M33), e.g. `qemu-system-arm -M mps2-an505` |
| `esp32c6.ld`        | ESP32-C6 HP core: code and data in SRAM (0x40800000-0x40860000), for loading with a debugger |
| `esp32c6-app.ld`    | ESP32-C6: the same, plus what the ESP-IDF bootloader needs to boot it from flash |

A custom board script defines the `CODE` and `RAM` memory regions and
`STACK_SIZE`, then does `INCLUDE cortexm-sections.ld` (ARM) or
`INCLUDE riscv-sections.ld` (RISC-V).

ESP32-C6 programs run entirely from SRAM. To boot one from flash with the
ESP-IDF second-stage bootloader already on the chip, link with
`esp32c6-app.ld`, convert and write it to the application partition:

```sh
esptool --chip esp32c6 elf2image --output hello-app.bin hello-esp32c6-app.elf
esptool --chip esp32c6 write-flash 0x10000 hello-app.bin
```

The ESP-IDF v6 bootloader requires an application descriptor
(`esp_app_desc_t`) at the start of the first segment and exactly two
flash-mapped segments; the runtime and `esp32c6-app.ld` provide both (the
code itself never runs from flash). Output then appears on the chip's USB
Serial/JTAG port.

### Bare-metal runtime behaviour

* The runtime provides the vector table and reset handler (FPU enable,
  `.data` copy, `.bss` clear, then `main`).
* The heap is the RAM between `.bss` and the stack (`STACK_SIZE` in the
  board script); on the MCX N947 RAM image that is about 110 KB. `free` and
  `realloc` give memory back: the allocator is a first-fit free list with
  boundary tags. Freed blocks are merged with free neighbours immediately,
  `realloc` grows into a free successor or shrinks in place, and blocks are
  8-byte aligned with 8 bytes of overhead each (16 and 16 on 64-bit). There
  is one free list, so allocation time grows with the number of free
  blocks. The freestanding x86-64 runtime uses the same allocator over
  `mmap`ed chunks; hosted Linux uses libc's `malloc`.
* Running out of memory stops the program with exit status 1 and a message
  on stderr giving the request and the heap's state (size, free bytes,
  largest free block). Freeing a pointer that is not an allocated block
  (a double free, a pointer not from the heap) stops it too.
* Cortex-M console: `print` and the exit status go through **semihosting**
  when a debugger handles it, otherwise to the board's **console**:
  * The MCX N947 board scripts select LPUART4 (FLEXCOMM4, P1_9 TX / P1_8 RX,
    115200 8N1), which the FRDM-MCXN947's MCU-Link presents as its virtual
    COM port (e.g. `/dev/serial/by-id/usb-NXP_Semiconductors_MCU-LINK_...-if02`).
    `\n` is sent as CR LF. A custom board script selects it with
    `__ojay_console_mcxn947 = 1;`.
  * A program can provide its own console by defining
    `__ojay_console_write(data: *u8, count: s64)`, for example
    `console :: (data: *u8, count: s64) #c_call #program_export "__ojay_console_write" { ... }`.
    It replaces the built-in UART.
  * Stdout and stderr share the console. Without semihosting the program
    ends in a `wfi` loop with r0 = 0xE417C0DE and r1 = its exit status.
* Cortex-M semihosting: a semihosting call halts the core whenever halting
  debug is enabled (DHCSR.C_DEBUGEN), even when the debugger that enabled it
  has gone, and that bit survives resets until a power cycle (LinkServer
  leaves it set). So on a board with a console the runtime only uses
  semihosting when the debugger announces itself by starting the program at
  `Reset_Handler` with r0 = 0x5E41D0B6 and r1 = its complement
  (`tools/run_mcxn947.py` does). Other debuggers (LinkServer, J-Link, pyOCD)
  show the output on the console instead, and a program started by a reset
  never halts in a semihosting call. On a board script without a console
  (and on QEMU, `mps2-an505.ld`) semihosting is used whenever C_DEBUGEN is
  set; with no debugger output is dropped.
* Faults print the fault type and CFSR/HFSR (ARM) or mcause/mepc/mtval
  (RISC-V), then exit with status 134.
* ESP32-C6: startup turns off the watchdogs (timer groups, RTC and super
  watchdog). Semihosting goes to the debugger's stdout/stderr; when no
  debugger is attached the semihosting trap is caught and output goes to the
  chip's USB Serial/JTAG port (e.g. `/dev/ttyACM0`) instead.

### Running on an ESP32-C6

`tools/run_esp32c6.py hello-esp32c6.elf` loads the image into SRAM through
the chip's built-in USB-JTAG with Espressif's OpenOCD (newest
`~/.espressif/tools/openocd-esp32`, or `$OJAY_ESP_OPENOCD`), runs it, forwards
its output and returns its exit status. Flash is not touched. With several
boards attached, pick one with `--usb-location <bus-port>` (see `lsusb -t`).
Espressif's OpenOCD does not map the `:tt` semihosting console, so the
runtime writes to the debugger's stdout/stderr handles (1 and 2) directly.

### Running on an FRDM-MCXN947

These are development tools, separate from the compiler:

* `tools/run_mcxn947.py image.elf` runs an image through the board's
  MCU-Link, forwarding its output and returning its exit status. RAM images
  (`mcxn947-ram.ld`) are loaded with OpenOCD (`tools/mcxn947.cfg`) without
  touching flash. Flash images need `--flash`: they are programmed with NXP
  LinkServer (`/opt/linkserver/LinkServer`, or set `OJAY_LINKSERVER`), then
  started from their reset vector under OpenOCD. `--uart` starts the program
  without announcing semihosting, reads its output from the virtual COM port
  (`--vcom` to choose the device) and its exit status from r0/r1, i.e. runs
  it as it runs without a debugger. `tests/run_tests.py --arm=board-uart`
  runs the tests that way (the ones that write to stderr then differ, as the
  UART carries both streams).
* The core's debug port only appears after a session is opened through NXP's
  Debug Mailbox; `tools/mcxn947.cfg` does this (it resets the chip).
* Flashing and a real cold boot can also be done directly with
  `LinkServer flash MCXN947:FRDM-MCXN947 load hello.elf`; the program then
  prints on the virtual COM port.

### Register definitions from SVD files and CMSIS headers

`tools/regs2jai.py` turns a CMSIS-SVD file or a CMSIS device header into a
Jai file to `#load`, following the CMSIS naming conventions so C code ports
over directly:

```sh
tools/regs2jai.py esp32c6.svd -o esp32c6.jai
tools/regs2jai.py MCXN947_cm33_core0.h -o mcxn947.jai -- \
    --target=arm-none-eabi -mcpu=cortex-m33 -DCPU_MCXN947VDF_cm33_core0 \
    -I CMSIS/Core/Include -I devices/MCXN/periph -I devices/MCXN/MCXN947
```

```jai
#load "mcxn947.jai";   // imports "Hardware" for volatile_load / volatile_store

volatile_store(*SYSCON0.AHBCLKCTRLSET[0], SYSCON_AHBCLKCTRL0_GPIO0(1));
volatile_store(*PORT0.PCR[10], PORT_PCR_MUX(0));
volatile_store(*GPIO0.PTOR, 1 << 10);
version := (volatile_load(*PORT0.VERID) & PORT_VERID_MAJOR_MASK) >> PORT_VERID_MAJOR_SHIFT;
```

Each register block becomes a struct (`GPIO_Type`, with a `#assert` on its
size); each peripheral gets `<NAME>_BASE` and a constant pointer `<NAME>`;
register fields get `_MASK` and `_SHIFT` constants and a helper procedure
like the C macro (`PORT_PCR_MUX(x)`).

* SVD: registers are placed at their offsets. Gaps become `_reserved` byte
  arrays, registers sharing an address become anonymous unions, and register
  and cluster arrays become Jai arrays. Peripherals with the same layout
  share a struct. Enumerated field values become constants
  (`<TYPE>_<REG>_<FIELD>_<VALUE>`) unless `--no-enums` is given. Interrupts
  become `<NAME>_IRQn` constants.
* Headers: the header goes through the C preprocessor (`clang -E`, or
  `--cc`). Compiler options after `--` select the target and the `#if`
  branches, as in a C build. The tool translates struct typedefs, enums,
  integer `#define`s, peripheral pointer `#define`s, and `NAME(x)` field
  macros. It skips what has no Jai equivalent, such as bitfield structs
  (the CMSIS core's `APSR_Type` and similar), inline functions, and macros
  that are not constants. Declarations from system headers are dropped;
  `--include` / `--exclude` (path regexes) choose the files.

It prints a warning for anything it has to skip or rename, such as duplicate
names in the source.

## Language support

Implemented:

* Declarations: `x := 1;`, `x: int = 1;`, `x: int;` (zero/default init),
  `= ---` (uninitialized), constants `N :: 10;`, typed constants
  `N : s32 : 10;`, multiple declarations `a, b := 1, 2;`
* Types: `s8..s64`, `u8..u64`, `int`, `float`/`float32`, `float64`, `bool`,
  `string`, `*T`, `[N]T`, `[]T`, `[..]T`, `Any`, `Type`, procedure types,
  `struct` (member defaults, anonymous `struct { }` / `union { }` members,
  `using` members, constants, `#place`, `#align`, `#this`), `union`, `enum` /
  `enum_flags` with explicit base types and values, type aliases, distinct
  types (`#type,distinct`) (see *Struct layout, constants and distinct types*
  below)
* Procedures: default and named arguments, multiple return values,
  variadic `..T` parameters (an array is passed to one as `f(..array)`, also
  to `print` / `sprint`: `print(format, ..args)`; parameters after the
  variadic one are passed by name, as in `join("a", "b", separator = ", ")`,
  and those without a default must be), procedure values, nested (non-capturing)
  procedures, short lambdas (see below), order-independent top-level declarations, `#foreign` /
  `#c_call` with `#system_library`, structs and unions by value across the
  C boundary, `#program_export` (see *C interop* below)
* Short lambdas: `(params) => expression` is a procedure whose body is
  `return expression;` (or just the expression when it has no value):
  `double :: (x: int) => x * 2;`, `apply((x) => x * x, 7)`. Parameters may
  leave out their types where the lambda is used as a procedure type (a
  typed variable, a parameter, `f: (int) -> int = (n) => n * 3;`), and take
  that type's parameter types, results and `#c_call`. Without `-> T` (which
  may be written: `(x: int) -> float => ...`) and without such a type, the
  results are the expression's type. For a polymorphic parameter like
  `f: (T) -> $R`, untyped parameters get the types the other arguments bind
  (`map(xs, (x) => x * 2)`), and the lambda's result binds `$R`. Like nested
  procedures, lambdas do not capture locals. Only the parenthesized form is
  supported (not `x => x * 2`).
* Overloading: procedures with the same name in a scope form an overload set
  (also across imported modules; user procedures can overload built-ins such
  as `max`). The best fit wins: exact types, then constants that fit, then
  implicit conversions; an equally good overload declared nearer to the call
  wins over one from an import.
* Polymorphic procedures: `$T` in a parameter type introduces a type
  variable (later uses are plain `T`), inferred from the arguments through
  `$T`, `*$T`, `[]$T`, `[..]$T`, `[$N]$T`, procedure types (`f: (T) -> $R`)
  and polymorphic struct instances (`Vector($T, $N)`). Untyped constants take
  their default type (`max_of(1, 2)` has `T = int`). Compile-time parameters
  are baked into the instance: `make :: ($T: Type) -> *T` is called as
  `make(int)`, and `$N: int` (or `$N := 10`) as `f(3, ...)`. In the body `T` is
  a type and a `Type` value (`size_of(T)`, `cast(T)`, `print("%", T)`). Each
  distinct set of bindings gets one instance. Polymorphic procedures take part
  in overloading (an equally good non-polymorphic overload wins) and work as
  operators and across modules; an instance is checked in the template's
  scope, and only when a call actually uses it.
* Polymorphic structs: `Vector :: struct ($T: Type, $N: int) { data: [N]T; }`,
  used as `Vector(float, 3)` in types and literals (`Vector(float, 3).{...}`),
  with default and named parameters (`Buffer(int, FILL = 7)`). Instances are
  cached by their parameter values, so `Vector(float32, 1 + 2)` is the same
  type, and are named like `Vector(float32, 3)` in diagnostics and `print`.
* Operator overloading: `operator + :: (a: V, b: V) -> V {...}` for binary
  operators, comparisons (`!=` falls back to `operator ==`), unary `-`,
  compound assignment (`operator +=`, else `a = a + b`), `#symmetric`, and
  indexing with `operator []`, `operator []=` and `operator *[]`
* `using`: struct members (`using pos: Vec2;`), parameters
  (`(using e: *Entity)`), local declarations (`using e := ...;`), and
  `using x;` / `using Enum_Type;` statements
* Statements: `if`/`else` (with or without `then`), `if x == { case ...; }`
  switches (on integers, enums, bools and strings) with `#through` and
  `#complete`, `while`, `for` over ranges, arrays and values with a
  `for_expansion` (`it`, `it_index`, named iterators, `for *`, `for <`,
  `remove it`), labeled
  `break`/`continue`, `defer`, `return`, compound assignment, parallel
  assignment
* Expressions: arithmetic, bitwise and shift operators, pointer arithmetic,
  `.*` / `<<` dereference, `cast(T)`, `cast,trunc(T)`, `xx`, `ifx`, struct
  literals `T.{...}` / `.{...}`, array literals `T.[...]` / `.[...]`, inferred
  enum members `.NAME`, `size_of`, `type_of`, `#char`, `#string`, `#file`,
  `#line`, `#if`, `#assert`, `#load`, `#location(x)` and `#caller_location`
  (see *Metaprogramming* below)
* Modules: `#import "Name"` finds `Name.jai` or `Name/module.jai` in a
  `modules` directory next to the importing file or in a `-I` directory. Each
  module has its own scope: its declarations are visible where it is
  imported, imports are not transitive, and a program's own declarations take
  precedence. `M :: #import "Name";` makes them available only as `M.name`
  (procedures, types, constants), for built-in modules too. Each file starts
  in `#scope_export`; declarations after `#scope_module` are visible only in
  the module's own files (in the main program that changes nothing), and
  after `#scope_file` only in their own file, so two files (of a module or
  the program, `#load`ed or not) can each have a private declaration of the
  same name. The directive in effect also applies inside top-level `#if` and
  to what a top-level `#insert` declares.
* Compile-time execution: `#run` (see *Compile-time execution* below)
* Metaprogramming: `#code`, `#insert`, `#expand` macros and `#modify` (see
  *Metaprogramming* below)
* Runtime type information: `type_info(T)`, `Type_Info` and the
  `Type_Info_*` structs (see *Runtime type information* below)
* Runtime checks: array bounds checks; `assert(cond)`, `assert(cond,
  "message")`, and `loc = ...` to report another location than the call
  (`check :: (x: int, loc := #caller_location) { assert(x > 0, loc = loc); }`)
* ojay extension for embedded targets: `#import "Hardware"` provides
  `volatile_load(p)` and `volatile_store(p, value)` for memory-mapped
  registers (integers, floats, enums, bools and pointers). Jai has no
  `volatile`, and without it the optimizer may merge or drop register
  accesses. `examples/blink.jai` uses them to blink the FRDM-MCXN947's LED.
  A pointer to a fixed address is a compile-time constant
  (`GPIO0 :: cast(*GPIO_Type) 0x4009_6000;`), so register blocks can be
  described as structs, as in CMSIS headers (see *Register definitions* below).
* The context and custom allocators (see *The context and allocators* below):
  `context`, `push_context`, `Context`, `Allocator`, `Allocator_Proc`,
  `Allocator_Mode`, `#add_context`, `#no_context`, temporary storage
  (`temporary_allocator`, `tprint`, `talloc`, `reset_temporary_storage`),
  `push_allocator`, and an allocator for each dynamic array
* Built-in modules: `Basic` (`print`, `sprint`/`tprint`, `write_string`,
  `alloc`, `free`, `realloc`, `New`, `NewArray`, `array_add`, `array_reserve`, `array_free`,
  `array_reset`, `assert`, `exit`, `memcpy`, `memset`, `min`, `max`,
  `to_c_string`, `get_command_line_arguments`, integer limits) and `Math` (`sqrt`, `sin`, `cos`, `tan`,
  `atan2`, `pow`, `floor`, `ceil`, `round`, `exp`, `log`, `abs`, `PI`, `TAU`).
  These are built into the compiler. Both modules also have a part written
  in Jai (`src/modules/Basic.jai`, `src/modules/Math.jai`, compiled into the
  compiler like the Preload; only what a program uses is checked and
  compiled, so the others cost nothing):
  * Basic: polymorphic array procedures (`array_resize`, `array_copy`,
    `array_find`, `array_add_if_unique`, `array_insert_at`,
    `array_ordered_remove_by_index` / `_by_value`,
    `array_unordered_remove_by_index` / `_by_value`, `array_view`, `peek`,
    `peek_pointer`, `pop`), `swap`, strings (`to_string` of a C string or of
    data and count, `c_style_strlen`, `alloc_string`, `copy_string`,
    `compare`, `join` (`separator`, `before_first`, `after_last`),
    `is_digit`, `is_alpha`, `is_alnum`, `is_space`),
    `String_Builder` (`init_string_builder`, `append` of a string or byte,
    `print_to_builder`, `builder_string_length`, `builder_to_string`,
    `reset`, `free_buffers`), and `log` / `log_error`, which format like
    `sprint` and call `context.logger` with a `Log_Info` (`location`, the
    caller's unless `loc` is given, `common_flags` from `flags`, and
    `user_flags`; `log_error` sets `Log_Flags.ERROR`, which the default logger
    writes to stderr).
  * Math: `clamp` (polymorphic), `lerp`, `PI64`, `TAU64`, `Vector2`,
    `Vector3`, `Vector4` (`x`, `y`, ... and `component[i]`), `make_vector2/3/4`,
    the operators `+ - * /` (per component, and `*` / `/` by a scalar),
    unary `-` and `==`, `dot`, `cross`, `length`, `length_squared`,
    `distance`, `normalize` (leaves vectors shorter than `epsilon`
    unchanged) and `lerp` of vectors.
  Differences from Jai's modules: `array_reserve` and `String_Builder`
  memory come from the array's allocator (`context.allocator` when it first
  grows), `String_Builder` is one growing buffer rather than a chain of
  buffers, `builder_to_string` takes no allocator, and there are no log
  sections (`Log_Info` has no `section`, `log` no `section` parameter).
  `print` shows anonymous struct and union members inline, so a `Vector3`
  prints as `{1, 2, 3}`.
  The allocating ones use `context.allocator`; `alloc`, `free`, `realloc`,
  `New` and `NewArray` also take an explicit allocator (`alloc(n, a)`,
  `New(T, allocator = a)`), and `New` / `NewArray` take `initialized = false`.
  `get_command_line_arguments()` returns the program's arguments as a
  `[] string`, the program name first; the array and strings belong to the
  runtime (do not free them). Bare-metal programs, and `#run` code, get an
  empty array.

What is not implemented yet is listed in [`TODO.md`](TODO.md). Some
directives are accepted and have no effect: `#no_abc`, `#must`,
`#deprecated` and call-site `inline`.

### Struct layout, constants and distinct types

```jai
S :: struct {
    N :: 3;                  // a constant: S.N, also s.N
    data: [N] int;
    T :: float32;            // a type constant: x: S.T;
    make :: () -> S { ... }  // a procedure constant: S.make()
    next: *#this;            // #this: the struct (in a polymorphic struct, the instance)
}
Overlay :: struct { a: s64; #place a; b: float64; }   // b overlaps a
Aligned :: struct { tag: u8; v: [4] float32 #align 16; }   // v at offset 16
Handle  :: #type,distinct u32;                          // a type of its own
```

* Constants: `name :: value;` (and `name : T : value;`) in a struct body.
  They take no space. Inside the body (member types, defaults, the
  procedures declared there) they are visible by name, outside as `S.N`,
  `value.N` or `pointer.N` (a member of the same name wins; the value before
  the `.` is not evaluated). A constant can be a number, string, bool, type,
  procedure (overloads, polymorphic procedures and `#expand` macros
  included), struct or enum. In a polymorphic struct each instance has its own
  (`Buf :: struct ($N: int) { SIZE :: N * 2; }`, `Buf(4).SIZE`). Constants
  made by an `#insert` in the body work the same way. They are resolved once
  the struct is laid out, so `N :: size_of(S);` works; a member type that
  needs such a constant is circular. Assigning to one is an error.
* `#place m;` before members makes the next member start at earlier member
  `m`'s offset (aligned up to its own alignment); the ones after it follow on
  from there, as Jai's pre-union idiom. The struct's size and alignment
  cover every member. Rules for initialization: two members that overlap may
  not both have a default value (their own or their type's); a struct literal
  may not set two members that overlap; and a member set in a literal
  replaces the defaults of the members it overlaps (they are not written at
  all, so the bytes outside the set member are zero). `print` shows every
  member, each read from its own offset. `#place` is not allowed in a union.
  A member of type `void` takes no space: it marks an offset for `#place`
  (`start: void; lo, hi: u16; #place start; whole: u32;`). It cannot have a
  value or be set in a literal (positional values skip it), `*s.start` is its
  address (a `*void`), `print` leaves it out, and Type_Info lists it with
  type `void`. Unions cannot have void members.
* `x: T #align N;` (after the type, or after `= value`): the member's offset
  is a multiple of N, a power-of-two constant, and the struct's alignment is
  at least N; it sets `Struct_Textual_Flags.ALIGNED`. Locals, globals,
  temporaries and struct members of such a type get that alignment. `New` and
  `NewArray` of a type aligned more than the heap guarantees (8 bytes on
  32-bit targets, 16 on x86-64) ask the runtime's heap for an aligned block,
  which `free` takes as usual; so do dynamic arrays of such a type
  (`array_add`, `array_reserve`), which move to a new aligned block when they
  grow instead of resizing in place. Other allocators are asked with the
  mode `ALLOCATE_ALIGNED` (an ojay extension: `size` bytes aligned to
  `old_size`), which `temporary_allocator` handles too; one that returns null
  for it (as Jai allocators do for modes they do not know) is asked with
  `ALLOCATE`, and a block that is then not aligned stops the program with a
  message.
* `#align N` also works on variables, local or global
  (`buffer: [64] u8 #align 32;`, `x := 0 #align 16;`), also in `#run` code;
  not on constants or parameters.
  `#align` on a struct (`} #align 64`) is rejected: in Jai it has no effect
  there.
* `#this` in a struct body is the struct (`next: *#this;`, the instance in a
  polymorphic struct); in a procedure body it is the procedure itself
  (`return n * #this(n - 1);`). Elsewhere, and in `#expand` macro bodies, it is
  an error.
* The layout is computed once (the type table), and codegen, the `#run`
  interpreter, `size_of`, `Type_Info` and the C ABI all use it, so it is the
  same on every target and at compile time. In LLVM, a struct with `#align`
  padding has explicit padding elements, and members that overlap an earlier
  one are reached by byte offset.
* `Name :: #type,distinct T;` makes a type with T's layout (and kind) that is
  not T: there is no implicit conversion between them, in either direction,
  nor to other types (also not `Handle` to `u64`). `cast` and `xx` convert
  either way (also between a distinct struct or array and its base). The
  operators of T work on two values of the distinct type, and an untyped
  constant converts to it (`h: Handle = 5;`, `h + 1` is a Handle), as for T
  itself; mixing it with a T (`h + u`) is a type mismatch. Overload
  resolution, polymorphic inference (`$T` binds to Handle) and `operator`
  overloads see it as a type of its own. It works for numbers, floats,
  strings, pointers, arrays, enums (their values) and structs (members,
  defaults, literals `Point.{1, 2}`, `S.N` constants), and across
  the C ABI it is its base type. `Alias :: Handle;` is an alias of it, not
  another distinct type. A distinct type must be declared as a named
  constant. `type_info(Handle)` is a `*Type_Info_Variant` (see *Runtime type
  information*), and diagnostics, `type_of` and `print` use its name.
* `Name :: #type,isa T;` is a distinct type that also converts implicitly to
  T (and from there as T does: an isa `u8` becomes an `s64` argument), but
  not back, nor to other distinct types. In overloading, converting to its
  own base is preferred to every other conversion, so `f(u32)` beats
  `f(s64)` for an isa `u32`, and an isa of an isa picks the nearer base.
  Mixed with its base in an operator (`i + u`), the result has the base
  type. Its Type_Info_Variant has `variant_flags` `.ISA`.
* Differences from Jai: struct constants' `offset_in_bytes` is 0; an untyped
  constant converts to a distinct numeric type implicitly; `#type,isa`
  follows the description above (Jai's exact rules for distinct and isa types
  are not documented publicly); Type_Info_Variant has only `name`,
  `variant_of` and `variant_flags`.

### C interop

```jai
libc :: #system_library "libc";
div_t :: struct { quot, rem: s32; }
div :: (numer: s32, denom: s32) -> div_t #foreign libc;   // a C function

Vec3 :: struct { x, y, z: float32; }
scale :: (v: Vec3, k: float32) -> Vec3 #c_call { return .{v.x * k, v.y * k, v.z * k}; }
apply :: (f: (Vec3, float32) -> Vec3 #c_call, v: Vec3) -> Vec3 #foreign;  // C calls f back
on_event :: (e: Event) -> s32 #c_call #program_export "app_on_event" { ... }  // C calls it by name
```

* `#foreign` procedures are C functions; `#c_call` procedures are Jai
  procedures with the C calling convention, which C can call through a
  function pointer (a procedure value) or, with `#program_export` (optionally
  `#program_export "symbol"`), by name. Calling a `#c_call` procedure from Jai,
  directly or through a value of a `#c_call` procedure type, uses the C
  convention too.
* Parameters and results can be integers, floats, `bool`, enums, pointers,
  procedure values, structs (nested, with fixed arrays) and unions. A Jai
  struct has the C layout of the same members in order: write the C side as
  `typedef struct { int8_t a; double d; } ID;` for `ID :: struct { a: s8;
  d: float64; }`; `bool` is C's `bool`, an `enum u8` is a `uint8_t`, `[3]u8` is
  `uint8_t name[3]`. Inside a struct, a `string` or `[]T` member is
  `struct { int64_t count; T *data; }` and an `Any` is two pointers.
* Not C types, so rejected as a parameter or result: `string`, array views,
  dynamic arrays, `Any`, `Type` and fixed arrays (C passes arrays as
  pointers; put the array in a struct). Also rejected: more than one return
  value and variadic `#foreign` procedures (C varargs).
* Structs with `#align` members are C structs with `_Alignas` members, and
  overlapping `#place` members are an anonymous union in C
  (`a: s64; #place a; lo, hi: u32;` is
  `struct { union { int64_t a; struct { uint32_t lo, hi; }; }; }`); both pass
  by value as clang and GCC pass those (`tests/run/c_abi_layout.c` checks the
  layout with `_Static_assert`). `void` members take no space. Distinct types
  are passed as their base type.
* Empty structs (`struct {}`) are what GNU C makes of `struct {}`: size 0,
  and as a parameter or result they take no register or stack slot (as with
  clang and GCC; C++ would give them size 1).
* The lowering follows each target's C ABI exactly as clang does, so C code
  compiled with clang or GCC links with it:
  * x86-64 Linux, System V AMD64 psABI 3.2.3: each eightbyte of a struct of
    up to 16 bytes is classified INTEGER or SSE and passed in general or
    `xmm` registers (`{float, float, float}` becomes `<2 x float>, float`);
    larger structs, and structs that no longer fit in the remaining
    registers (6 integer, 8 SSE), are copied to the stack (`byval`); results
    over 16 bytes are written to memory the caller passes (`sret`).
  * Arm Cortex-M33 with hard float, AAPCS32 §5.3.5, §6.4-6.5 (VFP variant):
    homogeneous aggregates of 1 to 4 `float32`s or 1 to 4 `float64`s go in
    VFP registers (s0-s15 / d0-d7) and come back in them; other structs are
    passed as 4-byte (or 8-byte, if 8-aligned) words in r0-r3 and then on the
    stack, split between them if needed (over 64 bytes `byval`). Results of
    up to 4 bytes come back in r0, larger ones through `sret`.
  * RISC-V ilp32 (ESP32-C6, soft float), RISC-V psABI integer calling
    convention: structs of up to 8 bytes in one or two registers (or a
    register and the stack); larger ones by reference to a copy the caller
    makes; results up to 8 bytes in a0/a1, larger ones through `sret`.
  * Integers narrower than 32 bits and `bool` are sign- or zero-extended as
    C expects (`signext` / `zeroext`).
* `#c_call` procedures have no context and no hidden parameter, so the C
  prototype is exactly the Jai signature.
* The classification is in `src/abi.cpp`. `tools/check_c_abi.py` compiles
  the same prototypes with clang and with each ojay compiler and compares
  the signatures (about 100 per target). `tests/run/c_abi.jai` with
  `c_abi.c` runs both directions against GCC-compiled C.
* On other targets (a `--target` triple not listed above) only scalars may
  cross the C boundary.

### Compile-time execution

`#run` executes code while compiling:

```jai
make_crc_table :: () -> [256]u32 { ... }
CRC_TABLE :: #run make_crc_table();     // a constant [256]u32
buffer: [#run compute_size()]u8;        // anywhere a constant is needed
#run print("built at compile time\n");  // a statement: run for its side effects
```

* `#run expr` is a constant of the expression's type. It works anywhere a
  constant does: constant declarations, array sizes, `#if`, `#assert`, `$N`
  arguments, struct member defaults, global initializers. Results can be
  integers, floats, bools, strings, enums, `Type`s, fixed arrays and structs
  (nested, including polymorphic struct instances). Indexing a constant array
  with a runtime index reads a read-only table in the object file.
* `#run expr;` as a statement (at top level or in a procedure body) runs
  during compilation and produces no code. Top-level ones run in source order
  after the program's declarations are resolved.
* The evaluated code can use locals, all statements (`if`, `while`, `for`,
  switches, `defer`, `break`/`continue`), calls (recursion, overloads,
  polymorphic procedures, operators, procedure values), pointers into its own
  memory, fixed arrays, views, dynamic arrays (`array_add`), strings, `New`,
  `alloc`/`free`, and the `Basic` and `Math` built-ins. `print` writes to the
  compiler's standard output.
* Global variables start with their initial values in each `#run`; writes to
  them stay local to that evaluation and do not change the compiled program.
* Values are laid out as on the target (sizes, alignment, struct offsets,
  4-byte pointers on ARM and RISC-V), so `size_of`, pointer arithmetic and
  casts agree with the compiled program when cross-compiling.
* Procedures are checked when `#run` first calls them. Polymorphic instances
  that only `#run` needs are not compiled into the object file.
* Errors: calling a `#foreign` procedure, `volatile_load` / `volatile_store`,
  dereferencing a fixed address (memory-mapped registers do not exist at
  compile time), a result that contains a pointer, an array view or dynamic
  array, an `Any`, a union or a procedure value, and run-time errors
  (division by zero, out-of-range shifts and float-to-int conversions,
  failed bounds checks, failed `assert`, `exit`, invalid memory accesses).
  The message shows the `#run` site and the call stack of the evaluation.
* Limits: 50 million steps (statements, loop iterations and calls; an
  infinite loop is reported after a few seconds), 20000 nested calls, 256 MB
  of heap and 16 MB of locals.
* The context works as at run time: `#run` code starts with the default
  context, can `push_context`, and can use allocators written in Jai, also
  ones that hand out memory from a buffer of the `#run` code (every access
  is still checked).
* Differences from Jai: `sin`, `pow` and other transcendental functions use
  the host's math library, whose last-bit rounding can differ from the
  target's libm.

### The context and allocators

```jai
Counter :: struct { allocs: int; }
counting :: (mode: Allocator_Mode, size: s64, old_size: s64, old_memory: *void, data: *void) -> *void {
    if mode == .ALLOCATE  (cast(*Counter) data).allocs += 1;
    return default_allocator_proc(mode, size, old_size, old_memory, null);
}

c: Counter;
ctx := context;
ctx.allocator = .{counting, *c};
push_context ctx {
    node := New(Node);          // alloc, New, NewArray, array_add, sprint,
    s := sprint("%", 1);        // to_c_string and free use context.allocator
}
p := alloc(64, Allocator.{counting, *c});   // or an explicit allocator
t := tprint("x = %", 5);                    // temporary storage
reset_temporary_storage();
```

* `Context` has `allocator`, `logger` / `logger_data` (the default logger
  writes the message to stdout, or stderr for `Log_Flags.ERROR`, adding a
  newline), `log_source_identifier`, `temporary_storage`, `thread_index`
  (always 0), `user_index` and `user_data`, plus the members added with
  `#add_context name: T = default;` (at top level, in any module).
* `context` is the running procedure's context. Assigning to it or its
  members (`context.allocator = a;`) changes that context, which is the
  caller's, as in Jai. `push_context c { ... }` runs the block with a copy
  of `c` and restores the previous context however the block is left: at
  its end, by `break` / `continue`, by `return` (after the block's defers,
  which still see the pushed context), or by a macro's `` `return``.
  `push_allocator(a)` makes `a` the allocator until the end of the calling
  scope.
* `Allocator :: struct { proc: Allocator_Proc; data: *void; }` and
  `Allocator_Proc :: #type (mode: Allocator_Mode, size: s64, old_size: s64,
  old_memory: *void, allocator_data: *void) -> *void`, with modes
  `ALLOCATE`, `RESIZE` (move to a block of `size` bytes, keeping `old_size`
  bytes; `old_memory` may be null), `FREE`, `FREE_ALL` and (an ojay
  extension) `ALLOCATE_ALIGNED` (see *Struct layout*). An allocator returns
  null for a mode it does not handle; one that switches on the mode with
  `#complete` must list `ALLOCATE_ALIGNED` too.
  `default_allocator_proc` is the runtime's heap. An allocator whose `proc`
  is null means `context.allocator`.
* A dynamic array (`[..]T`) has an `allocator` member, as in Jai: it takes
  `context.allocator` when it first grows and keeps it, so `array_add`,
  `array_free` and `array_reset` use the allocator its memory came from.
* Temporary storage: `temporary_allocator` hands out memory from 4 KB pages
  (bigger requests get a page of their own) taken from the heap when first
  needed; `FREE` does nothing, and `reset_temporary_storage()` frees it all,
  keeping the first page. `tprint` is `sprint` into temporary storage, and
  `talloc(n)` allocates there. `context.temporary_storage` has the usage and
  its high-water mark.
* `#c_call` and `#foreign` procedures, and procedures marked `#no_context`,
  have no context: using `context`, calling a procedure that needs one, or
  using an allocating built-in in them is an error, except inside a
  `push_context` block. A `#c_call` callback that needs a context saves
  one in a global beforehand and pushes it:
  `push_context saved_context { ... }`.
* Lowering: the context is one global pointer, `__ojay_context`, which
  points to a static default `Context` (the default allocator is
  `default_allocator_proc`). It needs no initialization at startup, so the C
  `main` that the object defines is unchanged. `push_context` stores the
  address of its copy there and the old pointer back when the block is left
  (on the same paths as `defer`). Procedures get no hidden parameter. Calls
  cost the same, and Jai procedures, `#c_call` procedures and C functions
  keep the same calling convention. The price is that the context is shared
  by everything that runs, which is fine without threads. An interrupt
  handler runs with the context of the code it interrupted; if it pushes one,
  it restores the pointer before returning.
* The Preload (`src/preload.jai`, compiled into the compiler) declares all
  of this in Jai. A program gets only the parts it uses. A program that uses
  none of it (no `context`, `push_context`, `Allocator`, `tprint`, ...)
  calls the runtime's heap directly. Otherwise the
  allocating built-ins go through `__allocate`, which calls the runtime
  directly when the allocator is the default one and makes an indirect call
  otherwise.
* Differences from Jai: `push_context` always pushes a copy;
  `ALLOCATE_ALIGNED` is ojay's own mode; `print` (which does not allocate)
  is allowed without a context.

### Runtime type information

```jai
describe :: (value: Any) {
    info := value.type;                       // a Type is a pointer to its Type_Info
    if info.type == .STRUCT {
        s := cast(*Type_Info_Struct) info;
        for s.members  print("% : % at %\n", it.name, cast(Type) it.type, it.offset_in_bytes);
    }
}
e := type_info(Color);                        // *Type_Info_Enum
for e.names  print("% = %\n", it, e.values[it_index]);
```

* Every type has a `Type_Info` in the compiled program (only for the types
  a program uses as values: `Any`, `type_of`, `type_info`, `Type`
  constants). A `Type` value points to it: `Type` converts implicitly to
  `*Type_Info`, a `*Type_Info_*` converts to `*Type_Info` and to `Type`, and
  `cast(*Type_Info_Struct) T` / `cast(Type) info` convert explicitly.
  Members of a `Type` value are those of its `Type_Info`
  (`value.type.runtime_size`). `type_info(T)` returns the struct for T's
  kind: `*Type_Info_Integer`, `_Float`, `_Pointer`, `_Array`, `_Struct`,
  `_Enum`, `_Procedure`, `_Variant` (distinct types), or `*Type_Info` for the
  others.
* `Type_Info` has `type` (a `Type_Info_Tag`: `INTEGER`, `FLOAT`, `BOOL`,
  `STRING`, `POINTER`, `PROCEDURE`, `VOID`, `STRUCT`, `ARRAY`, `ANY`, `ENUM`,
  `TYPE`, `CODE`, `VARIANT`) and `runtime_size`. The others start with
  `using info: Type_Info` and add: `signed` (integers); `pointer_to`;
  `element_type`, `array_type` (`FIXED`, `VIEW`, `RESIZABLE`) and
  `array_count` (-1 unless fixed); `name`, `members`
  (`Type_Info_Struct_Member`: `name`, `type`, `offset_in_bytes`, `flags`)
  and `textual_flags` (`.UNION`, `.ALIGNED` when a member has `#align`) for
  structs and unions; the members include the struct's constants in
  declaration order, with flag `CONSTANT`, offset 0 and the constant's type
  (`Type` for a type constant, the procedure type for a procedure; untyped
  numbers as `s64` / `float32`; polymorphic procedures and structs are not
  listed), and members that overlap (`#place`) have the same offsets; `name`,
  `internal_type`, `enum_type_flags` (`.FLAGS`), `names` and `values` for
  enums; `argument_types`, `return_types` and `procedure_flags`
  (`IS_C_CALL`, `HAS_VARARGS`) for procedure types; `name` and `variant_of`
  (the base type's Type_Info) and `variant_flags` (`.ISA` for `#type,isa`)
  for distinct types (`Type_Info_Variant`, tag
  `VARIANT`, with the base's `runtime_size`). Generic code follows
  `variant_of` to reach the base's kind; `print` does, and prints the name as
  the type. The declarations are in `src/core.jai`.
* `print` reads the same tables, so they are one copy per type in
  read-only data.
* Under `#run` the same structs are laid out in compile-time memory when
  first accessed, for the target's pointer size. Reading past the end of
  the actual `Type_Info_*` struct (a `Type_Info` cast to the wrong kind) is
  an error.
* Differences from Jai: the enums Jai nests inside the structs are
  top-level (`Type_Info_Array_Type`, `Type_Info_Struct_Member_Flags`,
  `Enum_Type_Flags`, `Type_Info_Procedure_Flags`); a distinct type is a
  `Type_Info_Variant` with `name` and `variant_of` (as Jai code uses them)
  and ojay's own `variant_flags`: Jai's full declaration is not public, so
  any further members it has are not reproduced; `ANONYMOUS` (0x100) marks
  anonymous struct and union members; and `Any.type` has type `Type`, which
  converts to `*Type_Info` as needed.

### Metaprogramming

```jai
// #insert: code made by #run (a string) or written with #code
unrolled :: (n: int) -> string { ... }   // "total += v[0]; total += v[1]; ..."
sum4 :: (v: [4]int) -> int { total := 0; #insert #run unrolled(4); return total; }
Vec :: struct { #insert #run "x, y, z: float;"; }

// #expand: inlined at each call; backticks reach the caller's scope
each_even :: (arr: []int, body: Code) #expand {
    `elem := 0;
    for arr  if it % 2 == 0 { `elem = it; #insert body; }
}
each_even(values, #code { total += elem; });

// #modify: runs when a polymorphic procedure is instantiated
add :: (a: $T, b: T) -> T #modify { if T == s16  T = s64; return true; } { return a + b; }
```

* `#code expr` and `#code { statements }` are constants of type `Code`: the
  syntax stays unchecked until it is inserted. A `Code` value can be a
  constant, an argument (of a macro, or a `$`-baked parameter, which gives
  each piece of code its own instance) or passed through `#run`; it cannot be
  printed or converted to `Any`.
* `#insert x;` takes a `Code` or string constant (typically `#insert #run
  f();`). Each use checks a fresh copy. As a statement, the code is checked
  in the current scope, so its declarations are visible after the `#insert`;
  as an expression (`x := #insert "a * 2";`) it replaces the expression. It
  also works at top level (declarations, evaluated once the unit's own
  declarations are known) and in struct bodies (members, also in polymorphic
  structs). Strings are parsed as source text; errors in them point into a
  file named `<inserted at file.jai:LINE>`, followed by the `#insert` site.
* `name :: (params) -> T #expand { ... }` is a macro: every call inlines a
  copy of the body. Parameters become locals of the caller, evaluated once,
  in order; `Code` parameters stay constants, so `#insert body;` works on
  them. Names in the body mean what they mean where the macro is declared;
  `` `x`` reads, assigns or calls a name of the caller's scope, and
  `` `x := v;`` declares one there: at the top level of the body in the
  caller's scope; nested in the body (e.g. in its loop, as a `for_expansion`
  does) in that block, where `Code` from the caller inserted inside the block
  sees it. A
  plain `return v;` ends the expansion with value `v`; `` `return`` returns
  from the caller, which for a macro expanded by another macro is that
  macro's expansion. `` `defer`` (at the top level of the body) defers to
  the end of the caller's scope. `break` and `continue` work on the caller's
  loops, and in inserted code on the innermost loop (the macro's own, as in a
  `for_expansion`). Macros can call macros; a compile-time parameter with
  `#if` ends recursion, and an expansion more than 100 levels deep is an
  error. Macros work in `#run` code too. Like polymorphic procedures, a
  macro's body is only checked where it is expanded.
* `Code` given to a macro is checked where it was written: it sees the
  caller's variables (and the backtick declarations the macro made there),
  and its `return` and backtick names are the caller's.
* `#modify { ... }` on a polymorphic procedure runs (in the interpreter)
  each time a call infers new bindings, with the type variables as `Type`
  variables. It returns a `bool`, optionally with a message
  (`return false, "why";`): `false` rejects the candidate, so overload
  resolution tries the others, and the message appears in the error.
  Assigning a type variable (`T = s64;`) changes the instance. Compile-time
  values (`$N`) are constants in it.
* Source locations: `Source_Code_Location` (`fully_pathed_filename`,
  `line_number`, `character_number`) is visible everywhere.
  `#location(x)` is a constant for where `x` is declared: a procedure (the
  first of an overload set), variable, constant, type or module member
  (`Math.clamp`); any other expression gives where that expression starts.
  Built-ins (`int`, `print`, `array_add`) have no location and are an error.
  `loc := #caller_location` is only allowed as a parameter's default value
  (of type `Source_Code_Location`): each call that leaves the argument out
  passes the location of the call (the callee's first character), which
  works with named arguments, overloads, `#run` and macros. The location is
  an ordinary argument, so a polymorphic procedure gets no instance per call
  site. A call through a procedure value has no defaults and must pass the
  location (`p(#location(x))` or any `Source_Code_Location`). Locations are
  where the call is written: a call in a macro body or a `for_expansion`
  reports the macro's line, a call in a body or `#code` the caller wrote
  reports that line, and text inserted from a string (which has no file of
  its own) reports the `#insert`. Files have an absolute path (diagnostics
  and `#file` keep the path as given); the embedded modules report their
  names, `<Basic>`, `<Math>`, `<preload>` and `<core>`. Both are constants,
  usable in `#run` code and as `#run` results.
* `for` over a value that is not an array or range calls the `#expand`
  macro `for_expansion(value, body, flags)`, as in Jai:

  ```jai
  for_expansion :: (list: *List($T), body: Code, flags: For_Flags) #expand {
      i := 0;
      while i < list.count {
          defer i += 1;
          #if flags & .POINTER  `it := *list.items[i];
          else                  `it := list.items[i];
          `it_index := i;
          #insert (remove = { list.items[i] = list.items[list.count - 1]; list.count -= 1; i -= 1; }) body;
      }
  }
  for list  print("% ", it);
  for x, i: list  if x == 0  remove x;
  ```

  The body is the `Code`; `flags` (`For_Flags`: `POINTER` for `for *`,
  `REVERSE` for `for <`) is a constant, so `#if` can test it. A first
  parameter that takes a pointer gets the value's address (when an overload
  takes one). The loop has its own scope, where the macro's `` `it`` and
  `` `it_index`` are declared; with `for x, i:` they are named `x` and `i`.
  `break`, `continue` and `return` in the body work as in other inserted
  code, and `break x` leaves the loop named `x`. `remove it` in the body runs
  the `remove` code of the `#insert (remove = { ... }) body;` that inserted
  it, in the macro's scope; without one it is an error.
* `remove it` (or `remove x` for a named iterator) in a `for` over a dynamic
  array or view (a variable) removes the current element by moving the last
  one into its place, as in Jai: the count shrinks, and going forward the
  moved element is visited next. Such a loop reads the array's count and
  data again on every iteration.
* `#if` takes a constant bool, number or enum (`#if flags & .REVERSE`).
* Differences from Jai: `assert` is a built-in rather than a macro, and
  takes no format arguments; the embedded modules report names rather than
  paths. A string inserted inside a macro
  resolves names in the macro's scope (the insertion point), while inserted
  `Code` follows the rules above.

## Layout

```
src/        compiler: lexer, parser, sema (types/checking), interp (#run), codegen (LLVM),
            abi (C calling conventions), driver
runtime/    C runtime (printing, allocation, startup) and board linker scripts
tools/      development tools: running images on the MCX N947 and ESP32-C6,
            converting SVD files / CMSIS headers to Jai, checking the C ABI against clang
tests/      run tests (programs + expected output, optionally a C file linked in) and error tests
examples/   sample programs
```
