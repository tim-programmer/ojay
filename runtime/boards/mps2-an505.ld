/* Arm MPS2+ FPGA with the AN505 image (Cortex-M33), as emulated by
 * `qemu-system-arm -M mps2-an505`. Code goes to SSRAM1 (secure alias) and
 * data to SSRAM2. */
MEMORY
{
  CODE (rwx) : ORIGIN = 0x10000000, LENGTH = 4M
  RAM  (rwx) : ORIGIN = 0x38000000, LENGTH = 2M
}
STACK_SIZE = 64K;
/* QEMU implements semihosting without a debugger attached. */
__ojay_semihost_always = 1;
INCLUDE cortexm-sections.ld
