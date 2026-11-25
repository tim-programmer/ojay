#include "driver.h"

int main(int argc, char** argv) {
  // 32-bit RISC-V microcontrollers. Default: the ESP32-C6 high-performance
  // core (RV32IMAC, no FPU, ilp32 ABI); float arithmetic is done in software
  // (libgcc at link time).
  static const ojay::TargetDesc target = {
      /*tool_name=*/"ojay-riscv",
      /*arch=*/"riscv32",
      /*triple=*/"riscv32-unknown-none-elf",
      /*cpu=*/"generic-rv32",
      /*features=*/"+m,+a,+c,+zicsr,+zifencei",
      /*pointer_size=*/4,
      /*platform=*/ojay::Platform::BareMetal,
      /*pic_by_default=*/false,
  };
  return ojay::driver_main(argc, argv, target);
}
