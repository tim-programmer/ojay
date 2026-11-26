// C calling-convention lowering; see abi.h.
//
// References (and the clang code each part mirrors, clang/lib/CodeGen/Targets):
// * x86-64: System V Application Binary Interface, AMD64 Architecture
//   Processor Supplement, section 3.2.3 "Parameter Passing" (X86.cpp,
//   X86_64ABIInfo).
// * Arm: Procedure Call Standard for the Arm Architecture (AAPCS32), sections
//   5.3.5 (homogeneous aggregates), 6.4 / 6.5 (parameter passing, result
//   return) (ARM.cpp, ARMABIInfo).
// * RISC-V: RISC-V ELF psABI, "Integer Calling Convention" (RISCV.cpp,
//   RISCVABIInfo).
// * AArch64: Procedure Call Standard for the Arm 64-bit Architecture
//   (AAPCS64), sections 5.9 (composite types), 6.8 / 6.9 (parameter passing,
//   result return) (AArch64.cpp, AArch64ABIInfo, its AAPCS kind).
// tools/check_c_abi.py compares the signatures produced here with clang's.
#include "abi.h"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
#include <stdexcept>

namespace ojay {

CAbi c_abi_for_triple(const std::string& triple) {
  llvm::Triple t(triple);
  if (t.getArch() == llvm::Triple::x86_64 && !t.isOSWindows()) return CAbi::X86_64_SysV;
  if (t.isARM() || t.isThumb()) {
    auto env = t.getEnvironment();
    bool hf = env == llvm::Triple::EABIHF || env == llvm::Triple::GNUEABIHF || env == llvm::Triple::MuslEABIHF;
    return hf ? CAbi::AAPCS_VFP : CAbi::AAPCS;
  }
  if (t.isRISCV()) return CAbi::RISCV;
  if (t.isAArch64() && !t.isOSDarwin() && !t.isOSWindows()) return CAbi::AAPCS64;
  return CAbi::None;
}

namespace {

bool is_scalar(const Type* t) {
  switch (t->kind) {
    case TypeKind::Int: case TypeKind::Float: case TypeKind::Bool: case TypeKind::Pointer:
    case TypeKind::Enum: case TypeKind::Proc:
      return true;
    default:
      return false;
  }
}

// Whether a struct member of type `t` has a C equivalent.
std::string member_unsupported(const Type* t) {
  switch (t->kind) {
    case TypeKind::Struct:
      for (auto& f : t->fields)
        if (std::string why = member_unsupported(f.type); !why.empty()) return why;
      return "";
    case TypeKind::Array:
      if (t->array_kind == ArrayKind::Fixed) return member_unsupported(t->elem);
      return "";  // views and dynamic arrays: passed as their layout ({count, data, ...})
    case TypeKind::Code:
      return "it contains a Code value, which exists only at compile time";
    default:
      return "";  // scalars, string ({count, data}), Any ({type, value_pointer}), Type (a pointer)
  }
}

int64_t align_up(int64_t v, int64_t a) { return (v + a - 1) / a * a; }

}  // namespace

std::string c_abi_unsupported(const Type* t) {
  if (is_scalar(t)) return "";
  switch (t->kind) {
    case TypeKind::Struct: return member_unsupported(t);
    case TypeKind::String:
      return "string is not a C type (pass s.data and s.count, or put the string in a struct)";
    case TypeKind::Array:
      if (t->array_kind == ArrayKind::Fixed)
        return "C does not pass arrays by value (use a pointer, or put the array in a struct)";
      return "array views and dynamic arrays are not C types (pass the data pointer and count, or put them in a struct)";
    case TypeKind::Any: return "Any is not a C type";
    case TypeKind::TypeT: return "Type values cannot be passed to C";
    case TypeKind::Code: return "Code exists only at compile time";
    default: return "it is not a C type";
  }
}

CAbiLowering::CAbiLowering(CAbi abi, llvm::LLVMContext& ctx, const llvm::DataLayout& dl, TypeTable& types,
                           std::function<llvm::Type*(Type*)> lt, int flen)
    : abi_(abi), flen_(abi == CAbi::RISCV ? flen : 0), ctx_(ctx), dl_(dl), T(types), lt_(std::move(lt)) {}

// ---------------------------------------------------------------- type structure

namespace {

struct Child {
  int64_t off;
  const Type* t;
};

// The members of an aggregate as (offset, type). Jai's built-in aggregates
// have the layout of the C structs shown in c_abi_unsupported.
std::vector<Child> children(const Type* t, TypeTable& T) {
  std::vector<Child> out;
  auto seq = [&](std::initializer_list<const Type*> ts) {
    int64_t off = 0;
    for (const Type* x : ts) {
      off = align_up(off, x->align);
      out.push_back({off, x});
      off += x->size;
    }
  };
  switch (t->kind) {
    case TypeKind::Struct:
      for (auto& f : t->fields) out.push_back({f.offset, f.type});
      break;
    case TypeKind::String:
    case TypeKind::Array:
      if (t->kind == TypeKind::Array && t->array_kind == ArrayKind::Dynamic)
        seq({T.s64, T.void_ptr, T.s64, T.void_ptr, T.void_ptr});
      else if (t->kind == TypeKind::String || t->array_kind == ArrayKind::View)
        seq({T.s64, T.void_ptr});
      break;
    case TypeKind::Any: seq({T.void_ptr, T.void_ptr}); break;
    default: break;
  }
  return out;
}

bool is_aggregate(const Type* t) { return !is_scalar(t) && t->kind != TypeKind::TypeT; }

// The member clang uses as a union's IR type: the most aligned, then the largest.
const Type* union_storage(const Type* t) {
  const Type* best = nullptr;
  for (auto& f : t->fields)
    if (!best || f.type->align > best->align || (f.type->align == best->align && f.type->size > best->size))
      best = f.type;
  return best;
}

bool is_union(const Type* t) { return t->kind == TypeKind::Struct && t->is_union; }

// Calls f(offset, scalar type) for every scalar inside `t`, unions included
// (all their members).
template <class F>
void for_each_leaf(const Type* t, int64_t off, TypeTable& T, F&& f) {
  if (t->size == 0) return;
  if (!is_aggregate(t) || t->kind == TypeKind::TypeT) {
    f(off, t);
    return;
  }
  if (t->kind == TypeKind::Array && t->array_kind == ArrayKind::Fixed) {
    for (int64_t i = 0; i < t->count; ++i) for_each_leaf(t->elem, off + i * t->elem->size, T, f);
    return;
  }
  for (const Child& c : children(t, T)) for_each_leaf(c.t, off + c.off, T, f);
}

// The member of a (non-union) struct-like aggregate containing byte `off`.
// Of members that overlap there (#place, which C writes as an anonymous
// union), the one clang takes as the union's IR type: the most aligned, then
// the largest, then the first.
const Child* child_at(const std::vector<Child>& cs, int64_t off) {
  const Child* found = nullptr;
  for (const Child& c : cs) {
    if (c.t->size == 0) continue;
    if (c.off <= off) found = &c;
  }
  const Child* best = nullptr;
  for (const Child& c : cs) {
    if (c.t->size == 0 || off < c.off || off >= c.off + c.t->size) continue;
    if (!best || c.t->align > best->t->align || (c.t->align == best->t->align && c.t->size > best->t->size))
      best = &c;
  }
  return best ? best : found;
}

}  // namespace

// ---------------------------------------------------------------- scalars

AbiArg CAbiLowering::scalar(Type* t, bool is_return) {
  AbiArg a;
  a.type = lt_(t);
  int bits = t->kind == TypeKind::Bool ? 1 : (t->kind == TypeKind::Int || t->kind == TypeKind::Enum) ? t->bits : 0;
  bool is_signed = t->kind != TypeKind::Bool && t->is_signed;
  // Integers narrower than 32 bits (and bool) are extended by the side that
  // produces them, as clang marks them. On RV64 32-bit integers are too,
  // always sign-extended (psABI: "32-bit values are sign-extended").
  if (bits > 0 && bits < 32 && abi_ != CAbi::AAPCS64) {  // (AAPCS64: the callee extends)
    a.kind = AbiArg::Extend;
    a.sign_ext = is_signed;
  } else if (bits == 32 && abi_ == CAbi::RISCV && dl_.getPointerSizeInBits() == 64) {
    a.kind = AbiArg::Extend;
    a.sign_ext = true;
  }
  (void)is_return;
  return a;
}

// ---------------------------------------------------------------- x86-64 SysV

// psABI 3.2.3: each eightbyte gets the merge of the classes of the scalars in it.
void CAbiLowering::x86_leaves(const Type* t, int64_t base, Class& lo, Class& hi) {
  for_each_leaf(t, base, T, [&](int64_t off, const Type* leaf) {
    Class c = leaf->kind == TypeKind::Float ? Class::SSE : Class::Integer;
    int64_t first = off / 8, last = (off + leaf->size - 1) / 8;
    if (first != last || first > 1) {  // unaligned: the whole argument goes to memory
      lo = hi = Class::Memory;
      return;
    }
    Class& slot = first == 0 ? lo : hi;
    if (slot == c || c == Class::NoClass) return;
    if (slot == Class::NoClass) slot = c;
    else if (slot == Class::Memory || c == Class::Memory) slot = Class::Memory;
    else slot = Class::Integer;  // INTEGER with SSE
  });
}

void CAbiLowering::x86_classify(const Type* t, Class& lo, Class& hi) {
  lo = hi = Class::NoClass;
  if (t->size > 16) {  // more than two eightbytes and no SSEUP: memory
    lo = Class::Memory;
    return;
  }
  x86_leaves(t, 0, lo, hi);
  if (hi == Class::Memory) lo = Class::Memory;
}

bool CAbiLowering::no_user_data(const Type* t, int64_t start_bit, int64_t end_bit) {
  bool empty = true;
  for_each_leaf(t, 0, T, [&](int64_t off, const Type* leaf) {
    int64_t s = off * 8, e = (off + leaf->size) * 8;
    if (s < end_bit && e > start_bit) empty = false;
  });
  return empty;
}

// clang's GetINTEGERTypeAtOffset: the integer type for the eightbyte at
// `ir_off` of `t` (which sits at `src_off` in the argument type `src`).
llvm::Type* CAbiLowering::x86_int_at(const Type* t, int64_t ir_off, const Type* src, int64_t src_off) {
  if (ir_off == 0) {
    if (t->kind == TypeKind::Pointer || t->kind == TypeKind::Proc || t->kind == TypeKind::TypeT)
      return llvm::PointerType::get(ctx_, 0);
    int bits = t->kind == TypeKind::Bool ? 8 : (t->kind == TypeKind::Int || t->kind == TypeKind::Enum) ? t->bits : 0;
    if (bits == 64) return llvm::IntegerType::get(ctx_, 64);
    if ((bits == 8 || bits == 16 || bits == 32) && no_user_data(src, src_off * 8 + bits, src_off * 8 + 64))
      return llvm::IntegerType::get(ctx_, bits);
  }
  if (is_union(t)) {
    const Type* m = union_storage(t);
    if (m && ir_off < m->size) return x86_int_at(m, ir_off, src, src_off);
  } else if (t->kind == TypeKind::Array && t->array_kind == ArrayKind::Fixed) {
    if (t->elem->size > 0) return x86_int_at(t->elem, ir_off % t->elem->size, src, src_off);
  } else if (is_aggregate(t) && ir_off < t->size) {
    auto cs = children(t, T);
    if (const Child* c = child_at(cs, ir_off)) return x86_int_at(c->t, ir_off - c->off, src, src_off);
  }
  return llvm::IntegerType::get(ctx_, std::min<int64_t>(src->size - src_off, 8) * 8);
}

// clang's getFPTypeAtOffset.
llvm::Type* CAbiLowering::x86_fp_at(const Type* t, int64_t off) {
  if (t->kind == TypeKind::Float)
    return off == 0 ? (t->bits == 32 ? llvm::Type::getFloatTy(ctx_) : llvm::Type::getDoubleTy(ctx_)) : nullptr;
  if (is_union(t)) {
    const Type* m = union_storage(t);
    return m && off < m->size ? x86_fp_at(m, off) : nullptr;
  }
  if (t->kind == TypeKind::Array && t->array_kind == ArrayKind::Fixed)
    return t->elem->size > 0 && off < t->size ? x86_fp_at(t->elem, off % t->elem->size) : nullptr;
  if (is_aggregate(t) && off < t->size) {
    auto cs = children(t, T);
    if (const Child* c = child_at(cs, off)) return x86_fp_at(c->t, off - c->off);
  }
  return nullptr;
}

// clang's GetSSETypeAtOffset: float, <2 x float> or double.
llvm::Type* CAbiLowering::x86_sse_at(const Type* t, int64_t ir_off, const Type* src, int64_t src_off) {
  int64_t source_size = src->size - src_off;
  llvm::Type* t0 = x86_fp_at(t, ir_off);
  if (!t0 || t0->isDoubleTy()) return llvm::Type::getDoubleTy(ctx_);
  llvm::Type* t1 = source_size > 4 ? x86_fp_at(t, ir_off + 4) : nullptr;
  if (!t1) return t0;
  if (t1->isFloatTy()) return llvm::FixedVectorType::get(t0, 2);
  return llvm::Type::getDoubleTy(ctx_);
}

// clang's GetX86_64ByValArgumentPair: {lo, hi} with hi at offset 8.
llvm::Type* CAbiLowering::x86_pair(llvm::Type* lo, llvm::Type* hi) {
  auto hi_align = dl_.getABITypeAlign(hi).value();
  if (align_up((int64_t)dl_.getTypeAllocSize(lo), hi_align) != 8) {
    if (lo->isHalfTy() || lo->isFloatTy()) lo = llvm::Type::getDoubleTy(ctx_);
    else lo = llvm::IntegerType::get(ctx_, 64);
  }
  return llvm::StructType::get(ctx_, {lo, hi});
}

// An argument that goes to the stack (clang's getIndirectResult).
AbiArg CAbiLowering::x86_indirect_arg(Type* t, int free_int) {
  if (!is_aggregate(t)) return scalar(t, false);
  AbiArg a;
  int64_t align = std::max<int64_t>(t->align, 8);
  // With every integer register taken, a small aggregate is passed as an
  // integer, which lands on the stack the same way as byval memory would.
  if (free_int == 0 && align == 8 && t->size <= 8) {
    a.type = llvm::IntegerType::get(ctx_, t->size * 8);
    return a;
  }
  a.kind = AbiArg::Indirect;
  a.byval = true;
  a.align = align;
  return a;
}

AbiArg CAbiLowering::x86_arg(Type* t, int free_int, int& need_int, int& need_sse) {
  need_int = need_sse = 0;
  if (!is_aggregate(t)) {
    if (t->kind == TypeKind::Float) need_sse = 1;
    else need_int = 1;
    return scalar(t, false);
  }
  Class lo, hi;
  x86_classify(t, lo, hi);
  if (lo == Class::Memory) return x86_indirect_arg(t, free_int);
  if (lo == Class::NoClass && hi == Class::NoClass) return AbiArg{AbiArg::Ignore};
  auto part = [&](Class c, int64_t off) -> llvm::Type* {
    if (c == Class::Integer) {
      ++need_int;
      return x86_int_at(t, off, t, off);
    }
    if (c == Class::SSE) {
      ++need_sse;
      return x86_sse_at(t, off, t, off);
    }
    return nullptr;
  };
  llvm::Type* lo_t = part(lo, 0);
  llvm::Type* hi_t = part(hi, 8);
  if (!lo_t) throw std::runtime_error("internal error: x86-64 argument whose first eightbyte is padding");
  AbiArg a;
  if (hi_t) {
    a.type = x86_pair(lo_t, hi_t);
    a.flatten = true;
  } else {
    a.type = lo_t;
  }
  return a;
}

AbiArg CAbiLowering::x86_return(Type* t) {
  if (!is_aggregate(t)) return scalar(t, true);
  Class lo, hi;
  x86_classify(t, lo, hi);
  if (lo == Class::Memory) {
    AbiArg a;
    a.kind = AbiArg::Indirect;
    a.align = t->align;
    return a;
  }
  if (lo == Class::NoClass && hi == Class::NoClass) return AbiArg{AbiArg::Ignore};
  auto part = [&](Class c, int64_t off) -> llvm::Type* {
    if (c == Class::Integer) return x86_int_at(t, off, t, off);
    if (c == Class::SSE) return x86_sse_at(t, off, t, off);
    return nullptr;
  };
  llvm::Type* lo_t = part(lo, 0);
  llvm::Type* hi_t = part(hi, 8);
  AbiArg a;
  a.type = hi_t ? x86_pair(lo_t, hi_t) : lo_t;
  return a;
}

// ---------------------------------------------------------------- Arm AAPCS

// AAPCS 5.3.5: a homogeneous aggregate of 1-4 floats or 1-4 doubles (clang's
// isHomogeneousAggregate; empty members are skipped, unions count their
// largest member, there may be no padding).
bool CAbiLowering::homogeneous(const Type* t, const Type*& base, uint64_t& members) {
  members = 0;
  if (t->kind == TypeKind::Array && t->array_kind == ArrayKind::Fixed) {
    if (t->count == 0) return false;
    uint64_t m = 0;
    if (!homogeneous(t->elem, base, m)) return false;
    members = m * t->count;
  } else if (t->kind == TypeKind::Struct && t->has_place && !t->is_union) {
    // Overlapping members are an anonymous union in C: the floats count once
    // per offset (a union counts its largest member).
    std::vector<int64_t> offsets;
    bool ok = true;
    for_each_leaf(t, 0, T, [&](int64_t off, const Type* leaf) {
      if (leaf->kind != TypeKind::Float) ok = false;
      else if (!base) base = leaf;
      else if (base->bits != leaf->bits) ok = false;
      if (std::find(offsets.begin(), offsets.end(), off) == offsets.end()) offsets.push_back(off);
    });
    if (!ok || !base) return false;
    members = offsets.size();
  } else if (t->kind == TypeKind::Struct) {
    for (auto& f : t->fields) {
      if (f.type->size == 0) continue;
      uint64_t m = 0;
      if (!homogeneous(f.type, base, m)) return false;
      members = t->is_union ? std::max(members, m) : members + m;
    }
    if (members == 0) return false;
  } else if (t->kind == TypeKind::Float) {
    if (!base) base = t;
    else if (base->bits != t->bits) return false;
    members = 1;
  } else {
    return false;
  }
  return members <= 4 && (uint64_t)t->size == base->size * members;
}

// The IR type of a homogeneous aggregate. LLVM recognizes it from the IR type
// itself, so that must be pure floats: the struct's own type when it is (as
// clang passes it), otherwise an equivalent literal struct.
llvm::Type* CAbiLowering::hfa_type(Type* t, const Type* base, uint64_t members) {
  std::function<bool(const Type*)> clean = [&](const Type* x) {
    if (x->kind == TypeKind::Float) return true;
    if (x->kind == TypeKind::Array && x->array_kind == ArrayKind::Fixed) return x->count > 0 && clean(x->elem);
    if (x->kind != TypeKind::Struct || x->is_union || x->fields.empty() || x->has_place || x->has_align) return false;
    for (auto& f : x->fields)
      if (!clean(f.type)) return false;
    return true;
  };
  if (clean(t)) return lt_(t);
  llvm::Type* b = base->bits == 32 ? llvm::Type::getFloatTy(ctx_) : llvm::Type::getDoubleTy(ctx_);
  return llvm::StructType::get(ctx_, std::vector<llvm::Type*>(members, b));
}

AbiArg CAbiLowering::arm_arg(Type* t) {
  if (!is_aggregate(t)) return scalar(t, false);
  if (t->size == 0) return AbiArg{AbiArg::Ignore};
  AbiArg a;
  const Type* base = nullptr;
  uint64_t members = 0;
  if (abi_ == CAbi::AAPCS_VFP && homogeneous(t, base, members)) {
    a.type = hfa_type(t, base, members);  // VFP registers (s0-s15 / d0-d7), else the stack
    return a;
  }
  if (t->size > 64) {  // clang: byval beyond 64 bytes (same memory image as the split form)
    a.kind = AbiArg::Indirect;
    a.byval = true;
    a.align = std::min<int64_t>(std::max<int64_t>(t->align, 4), 8);
    return a;
  }
  // AAPCS 6.5 C.3-C.5: the composite as words in r0-r3, then the stack,
  // split between them if needed; 8-byte aligned ones start at an even register.
  if (t->align <= 4) a.type = llvm::ArrayType::get(llvm::IntegerType::get(ctx_, 32), (t->size + 3) / 4);
  else a.type = llvm::ArrayType::get(llvm::IntegerType::get(ctx_, 64), (t->size + 7) / 8);
  return a;
}

AbiArg CAbiLowering::arm_return(Type* t) {
  if (!is_aggregate(t)) return scalar(t, true);
  if (t->size == 0) return AbiArg{AbiArg::Ignore};
  AbiArg a;
  const Type* base = nullptr;
  uint64_t members = 0;
  if (abi_ == CAbi::AAPCS_VFP && homogeneous(t, base, members)) {
    a.type = hfa_type(t, base, members);
    return a;
  }
  // AAPCS 6.4: a composite of up to 4 bytes comes back in r0; larger ones in
  // memory whose address is passed in r0.
  if (t->size <= 4) {
    a.type = llvm::IntegerType::get(ctx_, t->size <= 1 ? 8 : t->size <= 2 ? 16 : 32);
    return a;
  }
  a.kind = AbiArg::Indirect;
  a.align = t->align;
  return a;
}

// ---------------------------------------------------------------- AArch64

AbiArg CAbiLowering::aarch64_arg(Type* t, bool is_return) {
  if (!is_aggregate(t)) return scalar(t, is_return);
  if (t->size == 0) return AbiArg{AbiArg::Ignore};
  AbiArg a;
  const Type* base = nullptr;
  uint64_t members = 0;
  // 6.8 B.2 / C.2: a homogeneous floating-point aggregate goes in v0-v7 (all
  // of it, or on the stack); clang passes it as an array, returns it as itself.
  if (homogeneous(t, base, members)) {
    llvm::Type* b = base->bits == 32 ? llvm::Type::getFloatTy(ctx_) : llvm::Type::getDoubleTy(ctx_);
    a.type = is_return ? hfa_type(t, base, members) : llvm::ArrayType::get(b, members);
    return a;
  }
  // B.4: over 16 bytes, by reference: a copy the caller makes; results through x8 (sret).
  if (t->size > 16) {
    a.kind = AbiArg::Indirect;
    a.align = t->align;
    return a;
  }
  // C.9-C.11: one or two general registers. Results of up to 8 bytes as an
  // integer of their size; otherwise whole doublewords, as one i128 when the
  // composite is 16-byte aligned.
  if (is_return && t->size <= 8) {
    a.type = llvm::IntegerType::get(ctx_, (unsigned)(t->size * 8));
    return a;
  }
  int64_t bits = align_up(t->size * 8, 64);
  // A parameter made only of pointers (one per doubleword): as pointers (as clang passes it).
  int pointers = 0;
  bool only_pointers = true;
  for_each_leaf(t, 0, T, [&](int64_t off, const Type* leaf) {
    bool p = (leaf->kind == TypeKind::Pointer || leaf->kind == TypeKind::Proc) && leaf->size == 8 && off == 8 * pointers;
    if (p) ++pointers;
    else only_pointers = false;
  });
  if (!is_return && only_pointers && pointers * 8 == t->size && pointers >= 1) {
    llvm::Type* ptr = llvm::PointerType::get(ctx_, 0);
    a.type = pointers == 1 ? ptr : llvm::ArrayType::get(ptr, (uint64_t)pointers);
    return a;
  }
  if (t->align < 16 && bits == 128) a.type = llvm::ArrayType::get(llvm::IntegerType::get(ctx_, 64), 2);
  else a.type = llvm::IntegerType::get(ctx_, (unsigned)bits);
  return a;
}

// ---------------------------------------------------------------- RISC-V

// psABI integer calling convention: aggregates up to XLEN bits in one
// register, up to 2*XLEN in two (an aligned pair when 2*XLEN aligned) or a
// register and the stack, larger ones by reference to a copy made by the
// caller. Results the same, with "by reference" meaning sret.
// psABI "Hardware Floating-point Calling Convention": the scalars of a
// struct (nested structs and arrays flattened), if they are one float, two
// floats, or a float and an integer (floats of at most FLEN bits, integers of
// at most XLEN; no unions or pointers). Returns false if t is not like that.
bool CAbiLowering::riscv_fp_fields(const Type* t, int64_t off, const Type*& f1, int64_t& off1, const Type*& f2,
                                   int64_t& off2) {
  if (t->size == 0) return true;
  int64_t xlen = dl_.getPointerSizeInBits();
  switch (t->kind) {
    case TypeKind::Float: case TypeKind::Int: case TypeKind::Enum: case TypeKind::Bool: {
      bool is_float = t->kind == TypeKind::Float;
      if (is_float && t->size * 8 > flen_) return false;
      if (!is_float && t->size * 8 > xlen) return false;
      if (!is_float && f1 && f1->kind != TypeKind::Float) return false;  // int + int: not eligible
      if (!f1) { f1 = t; off1 = off; return true; }
      if (!f2) { f2 = t; off2 = off; return true; }
      return false;
    }
    case TypeKind::Array:
      if (t->array_kind != ArrayKind::Fixed) return false;
      for (int64_t i = 0; i < t->count; ++i)
        if (!riscv_fp_fields(t->elem, off + i * t->elem->size, f1, off1, f2, off2)) return false;
      return true;
    case TypeKind::Struct:
      if (t->is_union || t->has_place) return false;
      for (auto& f : t->fields)
        if (!riscv_fp_fields(f.type, off + f.offset, f1, off1, f2, off2)) return false;
      return true;
    default:
      return false;
  }
}

// `gprs` / `fprs`: the argument registers still free (a0-a7 / fa0-fa7), as
// clang counts them, since a struct only goes in FP registers if they suffice.
AbiArg CAbiLowering::riscv_arg(Type* t, bool is_return, int& gprs, int& fprs) {
  int64_t xlen = dl_.getPointerSizeInBits();
  auto take_gprs = [&](int64_t size_bits) {
    int need = size_bits > xlen && size_bits <= 2 * xlen ? 2 : 1;
    gprs -= std::min(need, gprs);
  };
  if (!is_aggregate(t)) {
    if (t->kind == TypeKind::Float && t->size * 8 <= flen_ && fprs > 0) --fprs;
    else take_gprs(t->size * 8);
    return scalar(t, is_return);
  }
  if (t->size == 0) return AbiArg{AbiArg::Ignore};
  AbiArg a;
  if (flen_) {
    const Type *f1 = nullptr, *f2 = nullptr;
    int64_t off1 = 0, off2 = 0;
    if (riscv_fp_fields(t, 0, f1, off1, f2, off2) && f1 && (f2 || f1->kind == TypeKind::Float)) {
      int need_f = (f1->kind == TypeKind::Float) + (f2 && f2->kind == TypeKind::Float);
      int need_g = (f1->kind != TypeKind::Float) + (f2 && f2->kind != TypeKind::Float);
      llvm::Type* t1 = lt_(const_cast<Type*>(f1));
      llvm::Type* t2 = f2 ? lt_(const_cast<Type*>(f2)) : nullptr;
      if (need_f <= fprs && need_g <= gprs) {
        fprs -= need_f;
        gprs -= need_g;
        if (!t2) {
          a.type = t1;
          if (off1 != 0) a.offsets = {off1};
        } else {
          a.type = llvm::StructType::get(ctx_, {t1, t2});
          a.flatten = !is_return;  // two arguments; a result comes back as the pair
          if (off1 != 0 || off2 != align_up(f1->size, f2->align)) a.offsets = {off1, off2};  // padding between
        }
        return a;
      }
    }
  }
  int64_t bits = t->size * 8;
  if (bits <= xlen) {
    a.type = llvm::IntegerType::get(ctx_, bits);
  } else if (bits <= 2 * xlen) {
    if (t->align * 8 == 2 * xlen) a.type = llvm::IntegerType::get(ctx_, 2 * xlen);
    else a.type = llvm::ArrayType::get(llvm::IntegerType::get(ctx_, xlen), 2);
  } else {
    a.kind = AbiArg::Indirect;
    a.align = t->align;
  }
  if (!is_return) take_gprs(a.kind == AbiArg::Indirect ? xlen : bits);
  return a;
}

// ---------------------------------------------------------------- signatures

AbiSignature CAbiLowering::classify(const Type* proc) {
  // C varargs (a #foreign procedure's last parameter `..`): the parameters
  // before it are the fixed ones; the variadic arguments (promoted by sema)
  // follow them as they are. Arm passes everything of a variadic call by the
  // base standard, also with hard float.
  bool variadic = proc->vararg >= 0;
  CAbi saved_abi = abi_;
  if (variadic && abi_ == CAbi::AAPCS_VFP) abi_ = CAbi::AAPCS;
  struct Restore {
    CAbi& a;
    CAbi v;
    ~Restore() { a = v; }
  } restore{abi_, saved_abi};
  std::vector<Type*> fixed(proc->params.begin(), variadic ? proc->params.begin() + proc->vararg : proc->params.end());
  AbiSignature s;
  bool has_aggregate = false;
  for (Type* p : fixed) has_aggregate |= is_aggregate(p);
  if (!proc->returns.empty()) has_aggregate |= is_aggregate(proc->returns[0]);
  if (has_aggregate && abi_ == CAbi::None)
    throw std::runtime_error("passing or returning structs by value in #c_call procedures is not supported on this target");

  if (proc->returns.empty()) {
    s.ret.kind = AbiArg::Ignore;
  } else {
    Type* r = proc->returns[0];
    switch (abi_) {
      case CAbi::X86_64_SysV: s.ret = x86_return(r); break;
      case CAbi::AAPCS: case CAbi::AAPCS_VFP: s.ret = arm_return(r); break;
      case CAbi::RISCV: {  // a result has fa0/fa1 and a0/a1
        int g = 2, f = flen_ ? 2 : 0;
        s.ret = riscv_arg(r, true, g, f);
        break;
      }
      case CAbi::AAPCS64: s.ret = aarch64_arg(r, true); break;
      case CAbi::None: s.ret = scalar(r, true); break;
    }
  }

  int free_int = 6, free_sse = 8;
  if (s.ret.kind == AbiArg::Indirect) --free_int;
  int rv_gprs = s.ret.kind == AbiArg::Indirect ? 7 : 8, rv_fprs = flen_ ? 8 : 0;
  for (Type* p : fixed) {
    AbiArg a;
    switch (abi_) {
      case CAbi::X86_64_SysV: {
        int need_int, need_sse;
        a = x86_arg(p, free_int, need_int, need_sse);
        if (a.kind != AbiArg::Indirect && free_int >= need_int && free_sse >= need_sse) {
          free_int -= need_int;
          free_sse -= need_sse;
        } else if (a.kind != AbiArg::Indirect) {
          a = x86_indirect_arg(p, free_int);
        }
        break;
      }
      case CAbi::AAPCS: case CAbi::AAPCS_VFP: a = arm_arg(p); break;
      case CAbi::RISCV: a = riscv_arg(p, false, rv_gprs, rv_fprs); break;
      case CAbi::AAPCS64: a = aarch64_arg(p, false); break;
      case CAbi::None: a = scalar(p, false); break;
    }
    s.params.push_back(a);
  }

  llvm::Type* ptr = llvm::PointerType::get(ctx_, 0);
  std::vector<llvm::Type*> irs;
  llvm::Type* ret = llvm::Type::getVoidTy(ctx_);
  if (s.ret.kind == AbiArg::Indirect) {
    s.sret_index = 0;
    irs.push_back(ptr);
  } else if (s.ret.kind != AbiArg::Ignore) {
    ret = s.ret.type;
  }
  for (const AbiArg& a : s.params) {
    s.first_ir_arg.push_back(a.kind == AbiArg::Ignore ? -1 : (int)irs.size());
    switch (a.kind) {
      case AbiArg::Ignore: break;
      case AbiArg::Indirect: irs.push_back(ptr); break;
      case AbiArg::Extend: irs.push_back(a.type); break;
      case AbiArg::Direct:
        if (a.flatten)
          for (llvm::Type* e : llvm::cast<llvm::StructType>(a.type)->elements()) irs.push_back(e);
        else
          irs.push_back(a.type);
        break;
    }
  }
  s.fn_type = llvm::FunctionType::get(ret, irs, variadic);
  return s;
}

}  // namespace ojay
