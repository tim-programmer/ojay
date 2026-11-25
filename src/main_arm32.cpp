#include "driver.h"

int main(int argc, char** argv) {
  // 32-bit ARM microcontrollers: Cortex-M33 (ARMv8-M Mainline, Thumb-2) with
  // the single-precision FPU and the hard-float EABI. float64 arithmetic is
  // done in software (libgcc's __aeabi_d* routines at link time).
  static const ojay::TargetDesc target = {
      /*tool_name=*/"ojay-arm32",
      /*arch=*/"arm32",
      /*triple=*/"thumbv8m.main-none-eabihf",
      /*cpu=*/"cortex-m33",
      /*features=*/"",
      /*pointer_size=*/4,
      /*platform=*/ojay::Platform::BareMetal,
      /*pic_by_default=*/false,
  };
  return ojay::driver_main(argc, argv, target);
}
