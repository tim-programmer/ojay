/* Section layout shared by all Cortex-M boards. A board script defines the
 * memory regions CODE (where code and constants live) and RAM, plus
 * STACK_SIZE, then INCLUDEs this file. */
ENTRY(Reset_Handler)

SECTIONS
{
  .text :
  {
    KEEP(*(.vectors))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > CODE

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > CODE
  .ARM.exidx : { *(.ARM.exidx* .gnu.linkonce.armexidx.*) } > CODE

  .data :
  {
    . = ALIGN(4);
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(4);
    __data_end = .;
  } > RAM AT > CODE
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    __bss_start = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(8);
    __bss_end = .;
  } > RAM

  __heap_start = __bss_end;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
  __heap_end = __stack_top - STACK_SIZE;
  ASSERT(__heap_end > __heap_start, "ojay: not enough RAM for the stack")
}
