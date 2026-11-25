#pragma once

namespace ojay {

enum class Platform {
  Linux,      // objects for Linux executables
  BareMetal,  // objects for microcontroller images
};

// Static description of one of the per-architecture compiler executables.
struct TargetDesc {
  const char* tool_name;  // e.g. "ojay-x86_64"
  const char* arch;       // e.g. "x86_64", "arm32"
  const char* triple;     // default LLVM target triple
  const char* cpu;        // default CPU
  const char* features;   // default target features
  int pointer_size;       // in bytes
  Platform platform;
  bool pic_by_default;    // emit position-independent code unless --no-pic
};

int driver_main(int argc, char** argv, const TargetDesc& target);

}  // namespace ojay
