# TODO

## Tooling

- [x] Generate a script that will convert from SVD files or CMSIS headers to importable jai code
      (`tools/regs2jai.py`).

## Language

- [x] `#run` (compile-time execution) (not yet: `#run,stallable` and the compiler message loop,
      unions, pointers and procedure values in results)
- [x] Polymorphic procedures and structs (`$T`) (not yet: `#modify`, `$$`, `#bake_arguments`,
      `/interface`, `$R` in return types, polymorphic procedure values)
- [x] `using` (not yet: pointer members, top level, `#as`)
- [x] Procedure overloading
- [x] Operator overloading
- [ ] Metaprogramming: `#insert`, `#code`, `#modify`, `#expand`
- [ ] The context and custom allocators
- [x] Named module imports (`Name :: #import "..."`) with a real namespace
- [ ] `#foreign` procedures that pass or return structs by value (C ABI lowering)
- [x] `if x == { case ...; }` switches on strings

## Runtime and modules

- [ ] A real allocator for bare-metal targets (`free` currently does not reclaim memory)
- [ ] `get_command_line_arguments()` (argc/argv are already saved by the runtime)
- [ ] Write more of `Basic` and `Math` in Jai once polymorphism exists
- [ ] Cortex-M: console output without a debugger (UART), and avoid halting in
      semihosting when DHCSR.C_DEBUGEN was left set by a detached debugger

## Targets and boards

- [ ] More RISC-V chips beyond the ESP32-C6
- [ ] Flash programming for the MCX N947 from OpenOCD (currently LinkServer only)
