# TODO

What is implemented is documented in `README.md`; this file lists only what is not done yet.

## Language

- [ ] Smaller directives and intrinsics: `#bytes`; `#asm` operands declared inline (Jai's
      `mov x:, 5`)
- [ ] Metaprogramming: `#insert (...)` options other than `remove`
- [ ] Compiler integration: `#compiler` procedures, the message loop (`#run,stallable`),
      `Code_Node` introspection (`compiler_get_nodes`), `#insert,scope(...)`, `#procedure_of_call`
- [ ] Runtime type info: notes on procedures and enums,
      the enums nested inside the structs as in Jai, `Type_Info_Overload_Set` and
      `Type_Info_Polymorphic_Variable`
- [ ] The other `Allocator_Mode`s (`STARTUP`, `CAPS`, ...)
- [ ] Threads: preemptive scheduling on bare metal (an RTOS; the scheduler is cooperative), wait
      timeouts measured against a clock there, atomics on other integer types than `s64`

## C ABI

- [ ] Other targets' C ABIs (only scalars cross the C boundary there): Apple's AArch64 variant,
      Windows x64; a runtime library for AArch64 Linux
- [ ] Runtime libraries for RISC-V ilp32f / ilp32d (the compilers support those ABIs)

## Runtime and modules

- [ ] More modules: `POSIX`, `Compiler`; `File` on bare metal without a debugger (flash storage)
- [ ] Basic and Math: moving `min` / `max` / `abs` from built-ins to Jai
- [ ] Command-line arguments: Jai's `__command_line_arguments` global (`get_command_line_arguments()` works)
- [ ] Cortex-M console: a UART for the MPS2 / QEMU board, separate stdout and stderr; console
      input with interrupts (it is polled, so the UART FIFO can overflow while the program does
      something else)

## Targets and boards

- [ ] Running under an RTOS (FreeRTOS, Zephyr), verified on the MCX N947:
      * a runtime library build without the startup code (vector table, `Reset_Handler`,
        `memcpy` / `memset`) whose heap is the RTOS's (`pvPortMalloc` / `k_malloc`: locked)
      * the context pointer per task: thread-local on Zephyr (`CONFIG_THREAD_LOCAL_STORAGE`,
        `__aeabi_read_tp`), switched in the task-switch hooks on FreeRTOS
      * a compiler option or `OS` value (`.FREERTOS`, `.ZEPHYR`) for the above
      * the Thread module over RTOS tasks, mutexes and semaphores (wait timeouts from the RTOS clock)
      * an example project for each
- [ ] More RISC-V chips beyond the ESP32-C6
