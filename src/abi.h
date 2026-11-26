// C calling-convention lowering for #c_call / #foreign procedures.
//
// Jai-to-Jai calls pass aggregates as LLVM first-class values. Calls that
// cross into or out of C must instead look exactly like what clang emits for
// the equivalent C prototype: aggregates are coerced to integer/float types,
// passed `byval`, by reference to a caller-made copy, or returned through an
// `sret` pointer, depending on the target. This file holds that
// classification, one function per target ABI; codegen applies the result to
// declarations, definitions, calls and indirect calls alike.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "types.h"

namespace llvm {
class DataLayout;
class FunctionType;
class LLVMContext;
class Type;
}  // namespace llvm

namespace ojay {

enum class CAbi {
  None,         // a target this file does not know: only scalars may cross
  X86_64_SysV,  // System V AMD64 psABI
  AAPCS,        // Arm AAPCS, base standard (soft float)
  AAPCS_VFP,    // Arm AAPCS, VFP variant (hard float)
  RISCV,        // RISC-V ilp32 / lp64, and with flen ilp32f / ilp32d / lp64d (hardware floating point)
  AAPCS64,      // Arm 64-bit AAPCS (Linux and other non-Apple systems)
};

CAbi c_abi_for_triple(const std::string& triple);

// Why a value of type `t` cannot be a parameter or the result of a #c_call /
// #foreign procedure, or "" when it can. Needs only the type's kind and
// members, not its layout, so it is safe while structs are being laid out.
std::string c_abi_unsupported(const Type* t);

// How one parameter, or the result, crosses the C boundary.
struct AbiArg {
  enum Kind {
    Direct,    // as `type`; when that is not the value's own LLVM type, through memory (a coerced load/store)
    Extend,    // a small integer or bool, as `type` with signext / zeroext
    Indirect,  // parameter: a pointer (`byval` memory, or a caller-made copy); result: an `sret` pointer
    Ignore,    // an empty struct: no IR argument / a void result
  };
  Kind kind = Direct;
  llvm::Type* type = nullptr;
  bool flatten = false;   // Direct parameter: `type` is a struct whose elements are separate IR arguments
  bool sign_ext = false;  // Extend: signext (else zeroext)
  bool byval = false;     // Indirect parameter: `byval` (else a plain pointer to a copy the caller made)
  uint64_t align = 0;     // Indirect: alignment of the memory
  // Direct: where the value's parts are in memory, when not where `type`'s
  // layout puts them (padding between them): the byte offset of each element
  // of `type` (a struct), or of `type` itself.
  std::vector<int64_t> offsets;
};

struct AbiSignature {
  AbiArg ret;
  std::vector<AbiArg> params;    // one per Jai parameter
  llvm::FunctionType* fn_type = nullptr;
  int sret_index = -1;           // IR argument index of the sret pointer, or -1
  std::vector<int> first_ir_arg; // per Jai parameter: its first IR argument, -1 when ignored
};

class CAbiLowering {
 public:
  // flen: RISC-V's floating-point registers' width for the ABI (32: ilp32f,
  // 64: ilp32d / lp64d), 0 for the integer ABIs.
  CAbiLowering(CAbi abi, llvm::LLVMContext& ctx, const llvm::DataLayout& dl, TypeTable& types,
               std::function<llvm::Type*(Type*)> lt, int flen = 0);

  // `proc` is a #c_call procedure type with at most one result.
  AbiSignature classify(const Type* proc);

 private:
  CAbi abi_;
  int flen_ = 0;
  llvm::LLVMContext& ctx_;
  const llvm::DataLayout& dl_;
  TypeTable& T;
  std::function<llvm::Type*(Type*)> lt_;

  AbiArg scalar(Type* t, bool is_return);

  // x86-64 SysV
  enum class Class { NoClass, Integer, SSE, Memory };
  void x86_classify(const Type* t, Class& lo, Class& hi);
  void x86_leaves(const Type* t, int64_t off, Class& lo, Class& hi);
  llvm::Type* x86_int_at(const Type* t, int64_t ir_off, const Type* src, int64_t src_off);
  llvm::Type* x86_sse_at(const Type* t, int64_t ir_off, const Type* src, int64_t src_off);
  llvm::Type* x86_fp_at(const Type* t, int64_t off);
  bool no_user_data(const Type* t, int64_t start_bit, int64_t end_bit);
  AbiArg x86_indirect_arg(Type* t, int free_int);
  AbiArg x86_arg(Type* t, int free_int, int& need_int, int& need_sse);
  AbiArg x86_return(Type* t);
  llvm::Type* x86_pair(llvm::Type* lo, llvm::Type* hi);

  // Arm AAPCS
  bool homogeneous(const Type* t, const Type*& base, uint64_t& members);
  llvm::Type* hfa_type(Type* t, const Type* base, uint64_t members);
  AbiArg arm_arg(Type* t);
  AbiArg arm_return(Type* t);

  // RISC-V
  AbiArg riscv_arg(Type* t, bool is_return, int& gprs, int& fprs);
  bool riscv_fp_fields(const Type* t, int64_t off, const Type*& f1, int64_t& off1, const Type*& f2, int64_t& off2);

  // AArch64
  AbiArg aarch64_arg(Type* t, bool is_return);
};

}  // namespace ojay
