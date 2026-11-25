/* Espressif ESP32-C6, high-performance core (RV32IMAC).
 *
 * The 512 KB HP SRAM at 0x40800000 is reachable as both instruction and data
 * memory. The image uses 0x40800000-0x40860000, which keeps clear of the
 * ESP-IDF second-stage bootloader (it lives at about 0x40866000-0x4087c610
 * while loading an application) and the ROM's data above it.
 *
 * Load the ELF with a debugger (e.g. OpenOCD), or convert it with
 * `esptool --chip esp32c6 elf2image` and flash it as the application of an
 * ESP-IDF bootloader. */
MEMORY
{
  CODE (rwx) : ORIGIN = 0x40800000, LENGTH = 192K
  RAM  (rwx) : ORIGIN = 0x40830000, LENGTH = 192K
}
STACK_SIZE = 16K;
INCLUDE riscv-sections.ld
