# TODO

What is implemented is documented in `README.md`; this file lists only what is not done yet.

## Language

- [ ] Smaller directives and intrinsics: `#bytes`, `#asm`
- [ ] Remaining polymorphism: polymorphic procedure literals as values
      (`f: (int) -> int = (x: $T) -> T { ... };`)
- [ ] Metaprogramming: `#insert (...)` options other than `remove`
- [ ] Compiler integration: `#compiler` procedures, the message loop (`#run,stallable`),
      `Code_Node` introspection (`compiler_get_nodes`), `#insert,scope(...)`, `#procedure_of_call`
- [ ] `#run` results that contain pointers, array views, dynamic arrays, `Any`, unions or
      procedure values (Jai can bake some of them, such as a `Context` or `Allocator`)
- [ ] Runtime type info: member `notes`, struct `initializer`, `polymorph_source_struct` and
      parameters, the enums nested inside the structs as in Jai, `Type_Info_Overload_Set` and
      `Type_Info_Polymorphic_Variable`
- [ ] The other `Allocator_Mode`s (`STARTUP`, `CAPS`, ...)
- [ ] Threads and thread contexts (the context is a single global pointer, so this means
      changing how the context is found)

## C ABI

- [ ] Other targets' C ABIs (only scalars cross the C boundary there)
- [ ] The RISC-V hard-float ABIs ilp32f / ilp32d

## Runtime and modules

- [ ] More modules: `String`, `File`, `Random`, `POSIX`, `Compiler`, `Thread`; `Hash_Table`'s
      custom hash and compare procedures (struct parameters that are procedures)
- [ ] Basic and Math: `Matrix` / `Quaternion`, Jai's chained `String_Builder` buffers, moving
      `min` / `max` / `abs` from built-ins to Jai, an allocator parameter for `builder_to_string`,
      log sections (`Log_Section`, `Log_Info.section`), format arguments for `assert`
      (`assert(x > 0, "x is %", x)`)
- [ ] Allocator: size-class bins for faster allocation with many free blocks
- [ ] Command-line arguments: `__command_line_arguments`, arguments for bare-metal programs (e.g.
      from semihosting `SYS_GET_CMDLINE`)
- [ ] Cortex-M console: input, a UART for the MPS2 / QEMU board, separate stdout and stderr

## Targets and boards

- [ ] More RISC-V chips beyond the ESP32-C6
- [ ] Flash programming for the MCX N947 from OpenOCD (currently LinkServer only)
