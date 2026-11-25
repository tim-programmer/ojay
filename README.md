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
* `print` and the exit status go through **semihosting**, so output appears
  in the debugger (OpenOCD, LinkServer, J-Link, pyOCD) or QEMU. With no
  debugger attached, output is dropped and the program halts in `wfi` on exit.
* Caveat (Cortex-M): the runtime uses semihosting whenever the core reports
  that debugging is enabled (DHCSR.C_DEBUGEN). That bit survives a reset, so
  after a debug session that left it set (LinkServer does), a program that
  prints halts at its first `print` until a debugger attaches or the board is
  power-cycled. Programs meant to run standalone, like `blink.jai`, should not
  print.
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
  started from their reset vector under OpenOCD.
* The core's debug port only appears after a session is opened through NXP's
  Debug Mailbox; `tools/mcxn947.cfg` does this (it resets the chip).
* Flashing and a real cold boot can also be done directly with
  `LinkServer run MCXN947:FRDM-MCXN947 hello.elf`, which prints the output but
  does not report the exit status.

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
  `using` members), `union`, `enum` / `enum_flags` with explicit base types and
  values, type aliases
* Procedures: default and named arguments, multiple return values,
  variadic `..T` parameters, procedure values, nested (non-capturing)
  procedures, order-independent top-level declarations, `#foreign` /
  `#c_call` with `#system_library`
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
  `#complete`, `while`, `for` over ranges and arrays
  (`it`, `it_index`, named iterators, `for *`, `for <`), labeled
  `break`/`continue`, `defer`, `return`, compound assignment, parallel
  assignment
* Expressions: arithmetic, bitwise and shift operators, pointer arithmetic,
  `.*` / `<<` dereference, `cast(T)`, `cast,trunc(T)`, `xx`, `ifx`, struct
  literals `T.{...}` / `.{...}`, array literals `T.[...]` / `.[...]`, inferred
  enum members `.NAME`, `size_of`, `type_of`, `#char`, `#string`, `#file`,
  `#line`, `#if`, `#assert`, `#load`
* Modules: `#import "Name"` finds `Name.jai` or `Name/module.jai` in a
  `modules` directory next to the importing file or in a `-I` directory. Each
  module has its own scope: its declarations are visible where it is
  imported, imports are not transitive, and a program's own declarations take
  precedence. `M :: #import "Name";` makes them available only as `M.name`
  (procedures, types, constants), for built-in modules too.
* Compile-time execution: `#run` (see *Compile-time execution* below)
* Metaprogramming: `#code`, `#insert`, `#expand` macros and `#modify` (see
  *Metaprogramming* below)
* Runtime checks: array bounds checks; `assert`
* ojay extension for embedded targets: `#import "Hardware"` provides
  `volatile_load(p)` and `volatile_store(p, value)` for memory-mapped
  registers (integers, floats, enums, bools and pointers). Jai has no
  `volatile`, and without it the optimizer may merge or drop register
  accesses. `examples/blink.jai` uses them to blink the FRDM-MCXN947's LED.
  A pointer to a fixed address is a compile-time constant
  (`GPIO0 :: cast(*GPIO_Type) 0x4009_6000;`), so register blocks can be
  described as structs, as in CMSIS headers (see *Register definitions* below).
* Built-in modules: `Basic` (`print`, `sprint`/`tprint`, `write_string`,
  `alloc`, `free`, `realloc`, `New`, `NewArray`, `array_add`, `array_free`,
  `array_reset`, `assert`, `exit`, `memcpy`, `memset`, `min`, `max`,
  `to_c_string`, integer limits) and `Math` (`sqrt`, `sin`, `cos`, `tan`,
  `atan2`, `pow`, `floor`, `ceil`, `round`, `exp`, `log`, `abs`, `PI`, `TAU`)

Not yet implemented: `#compiler` procedures and the compiler message loop
(`#run,stallable`), `Code_Node` introspection (`compiler_get_nodes`),
`#insert,scope(...)`, `#caller_location` / `#location`,
`#procedure_of_call`, the context and custom allocators, `#foreign`
procedures that pass or return structs by value, `using` on pointer members
and at top level, `#as`. Polymorphism does not yet cover `$$` auto-baking,
`#bake_arguments`, `$T/Interface` constraints, `$R` introduced in a return
type, `#modify` on structs, or polymorphic procedure values (using a template
without calling it).

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
* Differences from Jai: there is no compiler message loop, `#run,stallable`,
  context or allocator support; results cannot contain pointers (Jai can
  bake some of them into the program); `sin`, `pow` and other transcendental
  functions use the host's math library, whose last-bit rounding can differ
  from the target's libm.

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
  `` `x := v;`` declares one there (only at the top level of the body). A
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
* Differences from Jai: `#insert,scope()`, `#insert -> string { ... }`,
  `Code_Node` introspection, `#caller_location`, `` `break`` / `` `continue``
  and `#modify` on structs are not supported. `#code` takes an expression or
  a `{ }` block, not a bare statement. Macros return at most one value. A
  string inserted inside a macro resolves names in the macro's scope (the
  insertion point), while inserted `Code` follows the rules above.

## Layout

```
src/        compiler: lexer, parser, sema (types/checking), interp (#run), codegen (LLVM), driver
runtime/    C runtime (printing, allocation, startup) and board linker scripts
tools/      development tools: running images on the MCX N947 and ESP32-C6,
            converting SVD files / CMSIS headers to Jai
tests/      run tests (programs + expected output) and error tests
examples/   sample programs
```
