/* NXP MCX N947, core 0 (Cortex-M33): image in internal flash.
 * Flash is 2 MB at 0x00000000; the runtime uses the first 256 KB of the
 * contiguous system SRAM at 0x20000000. */
MEMORY
{
  CODE (rx)  : ORIGIN = 0x00000000, LENGTH = 2M
  RAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
}
STACK_SIZE = 32K;
INCLUDE cortexm-sections.ld
