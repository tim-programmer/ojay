// LLVM IR generation from a checked Program.
#pragma once

#include <memory>

#include "sema.h"

namespace llvm {
class LLVMContext;
class Module;
class DataLayout;
}  // namespace llvm

namespace ojay {

struct CodegenOptions {
  bool bounds_checks = true;
  bool short_enums = false;  // ARM EABI: objects declare variable-size enums
  std::string module_name = "jai";
};

// Generates an LLVM module. The module's data layout and triple must be set by
// the caller (via `configure`) before generation so type sizes match the target.
std::unique_ptr<llvm::Module> generate_module(llvm::LLVMContext& ctx, const llvm::DataLayout& dl,
                                              const std::string& triple, Program& prog, TypeTable& types,
                                              const SourceManager& sm, const CodegenOptions& opts);

}  // namespace ojay
