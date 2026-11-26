# TODO

## Tooling

- [x] Generate a script that will convert from SVD files or CMSIS headers to importable jai code
      (`tools/regs2jai.py`).

## Language

- [x] `#run` (compile-time execution) (not yet: `#run,stallable` and the compiler message loop,
      unions, pointers and procedure values in results)
- [x] Polymorphic procedures and structs (`$T`) (not yet: `$$`, `#bake_arguments`,
      `/interface`, `$R` in return types, polymorphic procedure values)
- [x] `using` (not yet: pointer members, top level, `#as`)
- [x] Procedure overloading
- [x] Operator overloading
- [x] Metaprogramming: `#insert`, `#code`, `#modify`, `#expand` (not yet: `#insert,scope()`,
      `Code_Node` introspection / `compiler_get_nodes`, `#caller_location`, `` `break``/`` `continue``,
      `#modify` on structs, macros with several return values)
- [x] The context and custom allocators (not yet: Basic's `log`, threads and thread contexts,
      the `STARTUP` / `CAPS` / ... allocator modes, alignment requests)
- [x] Named module imports (`Name :: #import "..."`) with a real namespace
- [x] `#foreign` procedures that pass or return structs by value (C ABI lowering) for x86-64 SysV,
      Arm AAPCS (VFP) and RISC-V ilp32, plus `#program_export` (not yet: variadic `#foreign`
      procedures, other targets' C ABIs, the RISC-V hard-float ABIs ilp32f/ilp32d)
- [x] `if x == { case ...; }` switches on strings

## Runtime and modules

- [x] A real allocator for bare-metal targets: a first-fit free list with boundary tags and
      immediate merging (not yet: size-class bins for faster allocation with many free blocks)
- [x] `get_command_line_arguments()` (not yet: `__command_line_arguments`, arguments for
      bare-metal programs, e.g. from semihosting `SYS_GET_CMDLINE`)
- [x] Write more of `Basic` and `Math` in Jai once polymorphism exists: `src/modules/Basic.jai`
      (polymorphic array procedures, `swap`, string procedures, `String_Builder`, `log`) and
      `src/modules/Math.jai` (`clamp`, `lerp`, `Vector2/3/4` with operators, `dot`, `cross`,
      `length`, `normalize`), plus `f(..array)` arguments and an `array_reserve` built-in
      (not yet: `join` and other procedures with parameters after a variadic one, `Matrix` /
      `Quaternion`, Jai's chained `String_Builder` buffers, moving `min` / `max` / `abs` from
      built-ins to Jai, the `String` module)
- [x] Cortex-M: console output without a debugger (UART), and avoid halting in
      semihosting when DHCSR.C_DEBUGEN was left set by a detached debugger: LPUART4 on the
      FRDM-MCXN947 or a program-defined `__ojay_console_write`; semihosting only when the
      debugger announces itself (not yet: console input, a UART for the MPS2 / QEMU board,
      separate stdout and stderr on the console)

## Targets and boards

- [ ] More RISC-V chips beyond the ESP32-C6
- [ ] Flash programming for the MCX N947 from OpenOCD (currently LinkServer only)
