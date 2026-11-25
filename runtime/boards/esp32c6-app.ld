/* Espressif ESP32-C6, high-performance core (RV32IMAC): bootable application
 * for the ESP-IDF second-stage bootloader.
 *
 * Convert the ELF with `esptool --chip esp32c6 elf2image`, then flash the
 * resulting .bin at the application partition's offset (usually 0x10000).
 *
 * Code and data run from SRAM exactly as with esp32c6.ld. The bootloader
 * additionally insists on two flash-mapped segments (it sets up the MMU from
 * them): the first must begin with the application descriptor
 * (esp_app_desc_t, provided by the runtime), the second is a placeholder.
 * Both start 0x20 bytes into a 64 KB page, which is where `esptool` places
 * the data of a flash segment (after the image and segment headers). */
MEMORY
{
  DROM (r)   : ORIGIN = 0x42000020, LENGTH = 64K - 0x20
  IROM (rx)  : ORIGIN = 0x42010020, LENGTH = 64K - 0x20
  CODE (rwx) : ORIGIN = 0x40800000, LENGTH = 192K
  RAM  (rwx) : ORIGIN = 0x40830000, LENGTH = 192K
}
STACK_SIZE = 16K;

SECTIONS
{
  .flash.appdesc : { KEEP(*(.ojay.image_header)) } > DROM
  .flash.placeholder : { LONG(0) } > IROM
}
INCLUDE riscv-sections.ld
