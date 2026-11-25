/* Section layout shared by RISC-V boards whose images run from RAM. A board
 * script defines the memory regions CODE and RAM plus STACK_SIZE, then
 * INCLUDEs this file.
 *
 * Initialized data is linked at its run address (no copy at startup), so the
 * same ELF can be loaded by a debugger or converted to a bootable image by a
 * tool that loads each section at its address (e.g. `esptool elf2image`). */
ENTRY(_start)

SECTIONS
{
  .text :
  {
    KEEP(*(.ojay.image_header))   /* e.g. ESP-IDF's app descriptor; must come first */
    KEEP(*(.text._start))
    *(.text .text.*)
    . = ALIGN(4);
  } > CODE

  .rodata :
  {
    *(.rodata .rodata.* .srodata .srodata.*)
    . = ALIGN(4);
  } > CODE

  .data :
  {
    . = ALIGN(4);
    __data_start = .;
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(4);
    __data_end = .;
  } > RAM
  __data_load = __data_start;

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > RAM

  __heap_start = __bss_end;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  __heap_end = __stack_top - STACK_SIZE;
  ASSERT(__heap_end > __heap_start, "ojay: not enough RAM for the stack")
}
