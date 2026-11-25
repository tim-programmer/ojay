/* NXP MCX N947, core 0 (Cortex-M33): image loaded into SRAM by a debugger
 * (nothing is written to flash). Code and data share the first 256 KB of
 * system SRAM at 0x20000000. */
MEMORY
{
  CODE (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
  RAM  (rwx) : ORIGIN = 0x20020000, LENGTH = 128K
}
STACK_SIZE = 16K;
/* Console without a debugger: LPUART4, the MCU-Link virtual COM port (115200 8N1). */
__ojay_console_mcxn947 = 1;
INCLUDE cortexm-sections.ld
