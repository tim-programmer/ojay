# TODO

What is implemented is documented in `README.md`; this file lists only what is not done yet.

## Language

- [ ] Smaller directives and intrinsics: `#bytes`, `#asm`
- [ ] Metaprogramming: `#insert (...)` options other than `remove`
- [ ] Compiler integration: `#compiler` procedures, the message loop (`#run,stallable`),
      `Code_Node` introspection (`compiler_get_nodes`), `#insert,scope(...)`, `#procedure_of_call`
- [ ] `#run` results that contain `*void` pointers or cycles of pointers
- [ ] Runtime type info: `polymorph_source_struct` and parameters, notes on procedures and enums,
      the enums nested inside the structs as in Jai, `Type_Info_Overload_Set` and
      `Type_Info_Polymorphic_Variable`
- [ ] The other `Allocator_Mode`s (`STARTUP`, `CAPS`, ...)
- [ ] Threads and thread contexts (the context is a single global pointer, so this means
      changing how the context is found)

## C ABI

- [ ] Other targets' C ABIs (only scalars cross the C boundary there)
- [ ] The RISC-V hard-float ABIs ilp32f / ilp32d

## Runtime and modules

- [ ] More modules: `File`, `POSIX`, `Compiler`, `Thread`; `Hash_Table`'s
      custom hash and compare procedures (struct parameters that are procedures)
- [ ] Basic and Math: `Matrix2`, Jai's chained `String_Builder` buffers, moving
      `min` / `max` / `abs` from built-ins to Jai, log sections (`Log_Section`, `Log_Info.section`)
- [ ] Allocator: size-class bins for faster allocation with many free blocks
- [ ] Command-line arguments: `__command_line_arguments`, arguments for bare-metal programs (e.g.
      from semihosting `SYS_GET_CMDLINE`)
- [ ] Cortex-M console: input, a UART for the MPS2 / QEMU board, separate stdout and stderr

## Targets and boards

- [ ] More RISC-V chips beyond the ESP32-C6
- [ ] Flash programming for the MCX N947 from OpenOCD (currently LinkServer only)
