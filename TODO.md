# TODO

What is implemented is documented in `README.md`; this file lists only what is not done yet.

## Language

- [ ] Smaller directives and intrinsics: `#bytes`; Jai's `#asm` register declarations
      (`reg: gpr;`, `===` pinning) and explicit clobber lists
- [ ] Metaprogramming: `#insert (...)` options other than `remove`
- [ ] Compiler integration: `#compiler` procedures, the message loop (`#run,stallable`),
      `Code_Node` introspection (`compiler_get_nodes`), `#insert,scope(...)`, `#procedure_of_call`
- [ ] `#run` results that contain `*void` pointers or cycles of pointers
- [ ] Runtime type info: `polymorph_source_struct` and parameters, notes on procedures and enums,
      the enums nested inside the structs as in Jai, `Type_Info_Overload_Set` and
      `Type_Info_Polymorphic_Variable`
- [ ] The other `Allocator_Mode`s (`STARTUP`, `CAPS`, ...)
- [ ] Threads on bare metal (an RTOS), and atomics other than x86-64's `atomic_add` / `atomic_swap`

## C ABI

- [ ] Other targets' C ABIs (only scalars cross the C boundary there): Apple's AArch64 variant,
      Windows x64; a runtime library for AArch64 Linux
- [ ] Runtime libraries for RISC-V ilp32f / ilp32d (the compilers support those ABIs)

## Runtime and modules

- [ ] More modules: `POSIX`, `Compiler`; `File` on bare metal (semihosting file calls)
- [ ] Basic and Math: `Matrix2`, Jai's chained `String_Builder` buffers, moving
      `min` / `max` / `abs` from built-ins to Jai
- [ ] Command-line arguments: Jai's `__command_line_arguments` global (`get_command_line_arguments()` works)
- [ ] Cortex-M console: input, a UART for the MPS2 / QEMU board, separate stdout and stderr

## Targets and boards

- [ ] More RISC-V chips beyond the ESP32-C6
- [ ] Flash programming for the MCX N947 from OpenOCD (currently LinkServer only)
