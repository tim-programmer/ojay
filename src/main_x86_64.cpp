#include "driver.h"

int main(int argc, char** argv) {
  static const ojay::TargetDesc target = {
      /*tool_name=*/"ojay-x86_64",
      /*arch=*/"x86_64",
      /*triple=*/"x86_64-pc-linux-gnu",
      /*cpu=*/"x86-64",
      /*features=*/"",
      /*pointer_size=*/8,
      /*platform=*/ojay::Platform::Linux,
      // PIC objects link into both PIE (the distro default) and static executables.
      /*pic_by_default=*/true,
  };
  return ojay::driver_main(argc, argv, target);
}
