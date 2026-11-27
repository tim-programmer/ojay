#include <set>
#include <unordered_set>
#include "codegen.h"

#include "abi.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/Support/Error.h>
#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <unordered_map>

namespace ojay {
namespace {

// Type_Info_Tag (core.jai, runtime/ojay_runtime.c)
enum TiTag : int64_t {
  TI_VARIANT = 15,
  TI_INTEGER = 0, TI_FLOAT = 1, TI_BOOL = 2, TI_STRING = 3, TI_POINTER = 4, TI_PROCEDURE = 5, TI_VOID = 6,
  TI_STRUCT = 7, TI_ARRAY = 8, TI_ANY = 10, TI_ENUM = 11, TI_TYPE = 13, TI_CODE = 14,
};

struct CgScope {
  std::vector<Stmt*> defers;
  Stmt* loop = nullptr;  // set on the marker scope of a loop
  llvm::BasicBlock* break_bb = nullptr;  // loops; for a macro expansion, its end
  llvm::BasicBlock* continue_bb = nullptr;
  MacroExpansion* expansion = nullptr;  // set on the top-level scope of an inlined #expand macro
  llvm::AllocaInst* result = nullptr;   // the expansion's value
  llvm::Value* restore_context = nullptr;  // push_context: the context pointer to restore when leaving
};

// Allocator_Mode values (preload.jai).
enum AllocatorMode : uint64_t { MODE_ALLOCATE = 0, MODE_RESIZE = 1, MODE_FREE = 2 };

class CodeGen {
 public:
  CodeGen(llvm::LLVMContext& ctx, const llvm::DataLayout& dl, const std::string& triple, Program& prog,
          TypeTable& types, const SourceManager& sm, const CodegenOptions& opts)
      : ctx_(ctx), dl_(dl), prog_(prog), T(types), sm_(sm), opts_(opts), b_(ctx) {
    mod_ = std::make_unique<llvm::Module>(opts.module_name, ctx);
    mod_->setDataLayout(dl);
    mod_->setTargetTriple(llvm::Triple(triple));
    mod_->addModuleFlag(llvm::Module::Error, "wchar_size", 4);
    if (opts.short_enums) mod_->addModuleFlag(llvm::Module::Error, "min_enum_size", 1);
    ptr_ = llvm::PointerType::get(ctx, 0);
    i1_ = llvm::Type::getInt1Ty(ctx);
    i8_ = llvm::Type::getInt8Ty(ctx);
    i32_ = llvm::Type::getInt32Ty(ctx);
    i64_ = llvm::Type::getInt64Ty(ctx);
    void_ = llvm::Type::getVoidTy(ctx);
    if (!opts.abi.empty())
      mod_->addModuleFlag(llvm::Module::Error, "target-abi", llvm::MDString::get(ctx, opts.abi));
    int flen = opts.abi.ends_with("d") ? 64 : opts.abi.ends_with("f") ? 32 : 0;  // ilp32d, ilp32f, lp64d, ...
    cabi_ = std::make_unique<CAbiLowering>(c_abi_for_triple(triple), ctx, dl, types,
                                           [this](Type* t) { return lt(t); }, flen);
  }

  std::unique_ptr<llvm::Module> run() {
    declare_runtime();
    // Foreign procedures first so they keep their exact symbol names.
    for (ProcLit* p : prog_.procs)
      if (p->is_foreign) declare_proc(p);
    for (ProcLit* p : prog_.procs)
      if (!p->is_foreign) declare_proc(p);
    // Globals are created first: an initializer can take the address of another.
    for (Symbol* g : prog_.global_vars) declare_global(g);
    for (Symbol* g : prog_.global_vars) init_global(g);
    if (prog_.uses_context) emit_context_pointer();
    for (ProcLit* p : prog_.procs)
      if (p->body) emit_proc_body(p);
    if (prog_.main_proc) emit_c_main();
    emit_dependent_libraries();

    std::string err;
    llvm::raw_string_ostream os(err);
    if (llvm::verifyModule(*mod_, &os)) {
      if (std::getenv("OJAY_DUMP_BAD_IR")) mod_->print(llvm::errs(), nullptr);
      throw CompileError(SourceLoc{}, "internal compiler error: invalid LLVM IR generated:\n" + os.str());
    }
    return std::move(mod_);
  }

 private:
  llvm::LLVMContext& ctx_;
  const llvm::DataLayout& dl_;
  Program& prog_;
  TypeTable& T;
  const SourceManager& sm_;
  CodegenOptions opts_;
  std::unique_ptr<llvm::Module> mod_;
  llvm::IRBuilder<> b_;

  llvm::Type* ptr_;
  llvm::Type* i1_;
  llvm::Type* i8_;
  llvm::Type* i32_;
  llvm::Type* i64_;
  llvm::Type* void_;

  llvm::Function* fn_ = nullptr;
  ProcLit* proc_ = nullptr;
  llvm::BasicBlock* entry_ = nullptr;
  std::vector<CgScope> scopes_;

  std::unordered_map<std::string, llvm::GlobalVariable*> strings_;
  std::unordered_map<Type*, llvm::Constant*> defaults_;
  std::unordered_map<const void*, llvm::GlobalVariable*> baked_objects_;  // ConstValue::Pointer objects
  // Structs whose members are not simply the LLVM struct's elements in order
  // (#align padding, #place overlaps): for each field, its element index, or
  // -1 for a member that overlaps an earlier one (reached by byte offset).
  struct StructLayout {
    std::vector<int> elem;
    bool simple = true;  // field i is element i, and there is no padding element
  };
  std::unordered_map<const Type*, StructLayout> layouts_;
  std::map<std::pair<const void*, Type*>, llvm::Constant*> const_globals_;

  // runtime functions
  llvm::FunctionCallee rt_print_, rt_sprint_, rt_write_, rt_alloc_, rt_free_, rt_realloc_, rt_bounds_,
      rt_assert_, rt_streq_, rt_exit_, rt_args_, rt_array_reserve_, rt_init_, rt_cstring_, rt_alloc_aligned_;

  // The context: a pointer to the current Context (the default context's global
  // initially), replaced by push_context. Only programs that use the context
  // (Program::uses_context) have it.
  llvm::GlobalVariable* context_ptr_ = nullptr;

  // C calling convention of #c_call procedure types (abi.h).
  std::unique_ptr<CAbiLowering> cabi_;
  std::unordered_map<const Type*, AbiSignature> c_sigs_;

  // ------------------------------------------------------------------ types

  llvm::Type* lt(Type* t) {
    if (t->llvm_type) return t->llvm_type;
    if (t->distinct_of) return t->llvm_type = lt(t->distinct_of);  // the base's representation
    llvm::Type* r = nullptr;
    switch (t->kind) {
      case TypeKind::Void: r = void_; break;
      case TypeKind::Bool: r = i1_; break;
      case TypeKind::Int: r = llvm::IntegerType::get(ctx_, t->bits); break;
      case TypeKind::Float: r = t->bits == 32 ? llvm::Type::getFloatTy(ctx_) : llvm::Type::getDoubleTy(ctx_); break;
      case TypeKind::String: r = llvm::StructType::get(ctx_, {i64_, ptr_}); break;
      case TypeKind::Pointer: case TypeKind::Proc: case TypeKind::TypeT: case TypeKind::Null:
        r = ptr_;
        break;
      case TypeKind::Any: r = llvm::StructType::get(ctx_, {ptr_, ptr_}); break;
      case TypeKind::Code: r = i64_; break;  // the compiler's id of the syntax tree
      case TypeKind::Enum: r = lt(t->elem); break;
      case TypeKind::UntypedInt: r = i64_; break;
      case TypeKind::UntypedFloat: r = llvm::Type::getFloatTy(ctx_); break;
      case TypeKind::Array:
        switch (t->array_kind) {
          case ArrayKind::Fixed: r = llvm::ArrayType::get(lt(t->elem), t->count); break;
          case ArrayKind::View: r = llvm::StructType::get(ctx_, {i64_, ptr_}); break;
          // count, data, allocated, allocator (proc, data)
          case ArrayKind::Dynamic: r = llvm::StructType::get(ctx_, {i64_, ptr_, i64_, ptr_, ptr_}); break;
        }
        break;
      case TypeKind::Struct: {
        auto* st = llvm::StructType::create(ctx_, t->name);
        t->llvm_type = st;  // set early: fields may point back to this struct
        if (t->is_union) {
          // Represent a union as its most-aligned member plus padding.
          llvm::Type* best = nullptr;
          int64_t best_align = 0, best_size = 0;
          for (auto& f : t->fields) {
            if (f.type->align > best_align) {
              best_align = f.type->align;
              best = lt(f.type);
              best_size = f.type->size;
            }
          }
          std::vector<llvm::Type*> body;
          if (best) body.push_back(best);
          if (t->size > best_size) body.push_back(llvm::ArrayType::get(i8_, t->size - best_size));
          st->setBody(body);
        } else {
          st->setBody(struct_body(t));
        }
        return st;
      }
      case TypeKind::Tuple: {
        std::vector<llvm::Type*> elems;
        for (Type* e : t->elems) elems.push_back(lt(e));
        r = llvm::StructType::get(ctx_, elems);
        break;
      }
    }
    t->llvm_type = r;
    return r;
  }

  // The elements of struct t's LLVM type: its members at the offsets the type
  // table gives (layout_struct), with i8 arrays as padding where LLVM's own
  // placement would differ (#align), and without the members that overlap an
  // earlier one (#place). Ordinary structs get exactly their members.
  std::vector<llvm::Type*> struct_body(Type* t) {
    StructLayout& L = layouts_[t];
    size_t n = t->fields.size();
    L.elem.assign(n, -1);
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return t->fields[a].offset < t->fields[b].offset; });
    std::vector<llvm::Type*> body;
    uint64_t cur = 0, al = 1;
    for (size_t k = 0; k < n; ++k) {
      size_t i = order[k];
      if (i != k) L.simple = false;
      const StructField& f = t->fields[i];
      if ((uint64_t)f.offset < cur || f.type->kind == TypeKind::Void) {  // overlaps an earlier member, or a void marker
        L.simple = false;
        continue;
      }
      llvm::Type* et = lt(f.type);
      uint64_t ea = dl_.getABITypeAlign(et).value();
      uint64_t natural = (cur + ea - 1) / ea * ea;
      if (natural > (uint64_t)f.offset)
        throw CompileError(SourceLoc{}, cat("internal error: member '", f.name, "' of ", type_name(t),
                                            " cannot be placed at offset ", f.offset));
      if (natural < (uint64_t)f.offset) {
        body.push_back(llvm::ArrayType::get(i8_, f.offset - cur));
        L.simple = false;
      }
      L.elem[i] = (int)body.size();
      body.push_back(et);
      cur = (uint64_t)f.offset + dl_.getTypeAllocSize(et);
      al = std::max(al, ea);
    }
    if ((cur + al - 1) / al * al != (uint64_t)t->size) {
      body.push_back(llvm::ArrayType::get(i8_, (uint64_t)t->size - cur));
      L.simple = false;
    }
    return body;
  }

  const StructLayout& layout(Type* t) {
    t = const_cast<Type*>(t->underlying());
    lt(t);
    return layouts_.at(t);
  }

  // Struct values that are handled in memory rather than as LLVM aggregates
  // (insertvalue / extractvalue): unions, and structs with padding or overlaps.
  bool in_memory(Type* t) { return t->is_union || !layout(t).simple; }

  // The address of field i of the struct (of type t) at `base`.
  llvm::Value* field_addr(Type* t, llvm::Value* base, int i) {
    if (t->is_union) return base;
    const StructLayout& L = layout(t);
    if (L.simple) return b_.CreateStructGEP(lt(t), base, (unsigned)i, t->fields[i].name);
    if (L.elem[i] >= 0) return b_.CreateStructGEP(lt(t), base, (unsigned)L.elem[i], t->fields[i].name);
    return b_.CreateConstInBoundsGEP1_64(i8_, base, (uint64_t)t->fields[i].offset, t->fields[i].name);
  }

  // The alignment of memory holding a t: LLVM's, or more for #align members.
  uint64_t align_of(Type* t) {
    uint64_t a = dl_.getPrefTypeAlign(mem_t(t)).value();
    return std::max<uint64_t>(a, t->kind == TypeKind::Void ? 1 : (uint64_t)t->align);
  }

  // Memory type: void has no storage, so pointers to void elements use i8.
  llvm::Type* mem_t(Type* t) { return t->kind == TypeKind::Void ? i8_ : lt(t); }

  llvm::FunctionType* fn_type(Type* p) {
    std::vector<llvm::Type*> params;
    for (Type* x : p->params) params.push_back(lt(x));
    llvm::Type* ret = void_;
    if (p->returns.size() == 1) ret = lt(p->returns[0]);
    else if (p->returns.size() > 1) ret = lt(T.tuple(p->returns));
    return llvm::FunctionType::get(ret, params, false);
  }

  uint64_t size_of(Type* t) { return t->kind == TypeKind::Void ? 0 : dl_.getTypeAllocSize(lt(t)); }

  // ------------------------------------------------------------------ C calling convention

  const AbiSignature& c_sig(Type* pt, SourceLoc loc = {}) {
    auto it = c_sigs_.find(pt);
    if (it != c_sigs_.end()) return it->second;
    for (size_t i = 0; i < pt->params.size(); ++i)  // (not the C varargs, which sema promoted)
      if (std::string why = c_abi_unsupported(pt->params[i]); (int)i != pt->vararg && !why.empty())
        throw CompileError(loc, cat("#c_call/#foreign parameters of type ", type_name(pt->params[i]),
                                    " are not supported: ", why));
    for (Type* x : pt->returns)
      if (std::string why = c_abi_unsupported(x); !why.empty())
        throw CompileError(loc, cat("#c_call/#foreign return type ", type_name(x), " is not supported: ", why));
    try {
      return c_sigs_.emplace(pt, cabi_->classify(pt)).first->second;
    } catch (const std::runtime_error& e) {
      throw CompileError(loc, e.what());
    }
  }

  // The LLVM function type of a procedure type: C's for #c_call ones.
  llvm::FunctionType* proc_fn_type(Type* pt, SourceLoc loc = {}) {
    return pt->c_call ? c_sig(pt, loc).fn_type : fn_type(pt);
  }

  // The parameter / result attributes clang puts on the C prototype (byval,
  // sret, alignment, sign/zero extension); used on definitions and call sites.
  llvm::AttributeList c_attrs(Type* pt) {
    const AbiSignature& s = c_sig(pt);
    llvm::AttributeList al;
    auto ext = [](const AbiArg& a) { return a.sign_ext ? llvm::Attribute::SExt : llvm::Attribute::ZExt; };
    if (s.ret.kind == AbiArg::Extend) al = al.addRetAttribute(ctx_, ext(s.ret));
    if (s.sret_index >= 0) {
      llvm::AttrBuilder b(ctx_);
      b.addStructRetAttr(lt(pt->returns[0]));
      b.addAlignmentAttr(llvm::Align(s.ret.align));
      b.addAttribute(llvm::Attribute::NoAlias);
      al = al.addParamAttributes(ctx_, s.sret_index, b);
    }
    for (size_t i = 0; i < s.params.size(); ++i) {
      const AbiArg& a = s.params[i];
      int idx = s.first_ir_arg[i];
      if (a.kind == AbiArg::Extend) {
        al = al.addParamAttribute(ctx_, idx, ext(a));
      } else if (a.kind == AbiArg::Indirect) {
        llvm::AttrBuilder b(ctx_);
        if (a.byval) b.addByValAttr(lt(pt->params[i]));
        b.addAlignmentAttr(llvm::Align(a.align));
        al = al.addParamAttributes(ctx_, idx, b);
      }
    }
    return al;
  }

  // Memory holding a value of type t that can also be read or written as `coerced`.
  llvm::AllocaInst* coerce_slot(Type* t, llvm::Type* coerced, uint64_t min_align = 1) {
    llvm::Type* vt = lt(t);
    uint64_t size = std::max<uint64_t>(dl_.getTypeAllocSize(vt), coerced ? dl_.getTypeAllocSize(coerced) : 0);
    uint64_t align = std::max<uint64_t>(align_of(t), min_align);
    if (coerced) align = std::max<uint64_t>(align, dl_.getABITypeAlign(coerced).value());
    auto* a = make_alloca_raw(llvm::ArrayType::get(i8_, std::max<uint64_t>(size, 1)), "c_abi");
    a->setAlignment(llvm::Align(align));
    return a;
  }

  // The address of element i of coerced type `ct` in `slot` (AbiArg::offsets when given).
  llvm::Value* coerced_elem(llvm::Type* ct, llvm::Value* slot, unsigned i, const std::vector<int64_t>& offsets) {
    if (!offsets.empty()) return b_.CreateConstInBoundsGEP1_64(i8_, slot, (uint64_t)offsets[i]);
    return b_.CreateStructGEP(ct, slot, i);
  }
  // Reads / writes a coerced value (`a.type`) in slot, part by part when a.offsets says where.
  llvm::Value* load_coerced(const AbiArg& a, llvm::Value* slot) {
    if (a.offsets.empty()) return b_.CreateLoad(a.type, slot);
    auto* st = llvm::dyn_cast<llvm::StructType>(a.type);
    if (!st) return b_.CreateLoad(a.type, coerced_elem(a.type, slot, 0, a.offsets));
    llvm::Value* v = llvm::UndefValue::get(st);
    for (unsigned i = 0; i < st->getNumElements(); ++i)
      v = b_.CreateInsertValue(v, b_.CreateLoad(st->getElementType(i), coerced_elem(st, slot, i, a.offsets)), i);
    return v;
  }
  void store_coerced(const AbiArg& a, llvm::Value* v, llvm::Value* slot) {
    if (a.offsets.empty()) {
      b_.CreateStore(v, slot);
      return;
    }
    auto* st = llvm::dyn_cast<llvm::StructType>(a.type);
    if (!st) {
      b_.CreateStore(v, coerced_elem(a.type, slot, 0, a.offsets));
      return;
    }
    for (unsigned i = 0; i < st->getNumElements(); ++i)
      b_.CreateStore(b_.CreateExtractValue(v, i), coerced_elem(st, slot, i, a.offsets));
  }

  // The IR arguments for one Jai argument value `v` of type t.
  void lower_c_arg(const AbiArg& a, Type* t, llvm::Value* v, std::vector<llvm::Value*>& out) {
    switch (a.kind) {
      case AbiArg::Ignore: return;
      case AbiArg::Extend: out.push_back(v); return;
      case AbiArg::Indirect: {  // byval memory, or the copy the callee may modify
        auto* slot = coerce_slot(t, nullptr, a.align);
        b_.CreateStore(v, slot);
        out.push_back(slot);
        return;
      }
      case AbiArg::Direct: {
        if (a.type == lt(t) && a.offsets.empty()) {
          out.push_back(v);
          return;
        }
        auto* slot = coerce_slot(t, a.type);
        b_.CreateStore(v, slot);
        if (a.flatten) {
          auto* st = llvm::cast<llvm::StructType>(a.type);
          for (unsigned i = 0; i < st->getNumElements(); ++i)
            out.push_back(b_.CreateLoad(st->getElementType(i), coerced_elem(st, slot, i, a.offsets)));
        } else {
          out.push_back(load_coerced(a, slot));
        }
        return;
      }
    }
  }

  // Calls a #c_call procedure (direct or through a procedure value).
  llvm::Value* emit_c_call(Call* c, llvm::Value* callee) {
    Type* pt = c->proc_type;
    const AbiSignature& s = c_sig(pt, c->loc);
    std::vector<llvm::Value*> args;
    llvm::AllocaInst* sret = nullptr;
    if (s.sret_index >= 0) {
      sret = coerce_slot(pt->returns[0], nullptr, s.ret.align);
      args.push_back(sret);
    }
    for (size_t i = 0; i < s.params.size(); ++i)
      lower_c_arg(s.params[i], pt->params[i], rvalue(c->resolved_args[i]), args);
    for (Expr* v : c->c_varargs) args.push_back(rvalue(v));
    auto* call = b_.CreateCall(s.fn_type, callee, args);
    call->setAttributes(c_attrs(pt));
    if (c->force_inline) call->addFnAttr(llvm::Attribute::AlwaysInline);
    if (pt->returns.empty()) return nullptr;
    Type* rt = pt->returns[0];
    switch (s.ret.kind) {
      case AbiArg::Ignore: return llvm::Constant::getNullValue(lt(rt));
      case AbiArg::Indirect: return b_.CreateLoad(lt(rt), sret);
      case AbiArg::Extend: return call;
      case AbiArg::Direct: {
        if (s.ret.type == lt(rt) && s.ret.offsets.empty()) return call;
        auto* slot = coerce_slot(rt, s.ret.type);
        store_coerced(s.ret, call, slot);
        return b_.CreateLoad(lt(rt), slot);
      }
    }
    return nullptr;
  }

  // Entry of a #c_call procedure defined in Jai: gives each parameter its storage.
  void c_prologue(ProcLit* p) {
    const AbiSignature& s = c_sig(p->type, p->loc);
    for (size_t i = 0; i < p->params.size(); ++i) {
      Symbol* sym = p->params[i]->syms[0];
      const AbiArg& a = s.params[i];
      int idx = s.first_ir_arg[i];
      if (a.kind == AbiArg::Indirect) {  // the memory is this call's own (byval, or a copy made by the caller)
        sym->llvm_value = fn_->getArg(idx);
        continue;
      }
      if (a.kind == AbiArg::Ignore || a.kind == AbiArg::Extend || (a.type == lt(sym->type) && a.offsets.empty())) {
        auto* slot = make_alloca(sym->type, sym->name);
        if (a.kind != AbiArg::Ignore) b_.CreateStore(fn_->getArg(idx), slot);
        sym->llvm_value = slot;
        continue;
      }
      auto* slot = coerce_slot(sym->type, a.type);
      if (a.flatten) {
        auto* st = llvm::cast<llvm::StructType>(a.type);
        for (unsigned j = 0; j < st->getNumElements(); ++j)
          b_.CreateStore(fn_->getArg(idx + j), coerced_elem(st, slot, j, a.offsets));
      } else {
        store_coerced(a, fn_->getArg(idx), slot);
      }
      sym->llvm_value = slot;
    }
  }

  // `return v` from a #c_call procedure defined in Jai.
  void emit_c_ret(llvm::Value* v) {
    const AbiSignature& s = c_sig(proc_->type);
    Type* rt = proc_->type->returns.empty() ? nullptr : proc_->type->returns[0];
    switch (s.ret.kind) {
      case AbiArg::Ignore: b_.CreateRetVoid(); return;
      case AbiArg::Indirect:
        b_.CreateStore(v, fn_->getArg(s.sret_index));
        b_.CreateRetVoid();
        return;
      case AbiArg::Extend: b_.CreateRet(v); return;
      case AbiArg::Direct: {
        if (s.ret.type == lt(rt) && s.ret.offsets.empty()) {
          b_.CreateRet(v);
          return;
        }
        auto* slot = coerce_slot(rt, s.ret.type);
        b_.CreateStore(v, slot);
        b_.CreateRet(load_coerced(s.ret, slot));
        return;
      }
    }
  }

  // ------------------------------------------------------------------ runtime

  llvm::FunctionCallee rt(const char* name, llvm::Type* ret, std::vector<llvm::Type*> params, bool noreturn = false) {
    auto* ft = llvm::FunctionType::get(ret, params, false);
    auto callee = mod_->getOrInsertFunction(name, ft);
    if (auto* f = llvm::dyn_cast<llvm::Function>(callee.getCallee())) {
      f->addFnAttr(llvm::Attribute::NoUnwind);
      if (noreturn) f->addFnAttr(llvm::Attribute::NoReturn);
    }
    return callee;
  }

  void declare_runtime() {
    rt_print_ = rt("__ojay_print", i64_, {ptr_, i64_, ptr_, i64_, i32_});
    rt_sprint_ = rt("__ojay_sprint", void_, {ptr_, ptr_, i64_, ptr_, i64_, ptr_, ptr_});
    rt_write_ = rt("__ojay_write_string", void_, {ptr_, i64_, i32_});
    rt_alloc_ = rt("__ojay_alloc", ptr_, {i64_});
    rt_alloc_aligned_ = rt("__ojay_alloc_aligned", ptr_, {i64_, i64_});
    rt_free_ = rt("__ojay_free", void_, {ptr_});
    rt_realloc_ = rt("__ojay_realloc", ptr_, {ptr_, i64_, i64_});
    rt_bounds_ = rt("__ojay_bounds_fail", void_, {i64_, i64_, ptr_, i64_, i32_}, true);
    rt_assert_ = rt("__ojay_assert_fail", void_, {ptr_, i64_, i32_, ptr_, i64_}, true);
    rt_streq_ = rt("__ojay_string_equal", i32_, {ptr_, i64_, ptr_, i64_});
    rt_exit_ = rt("__ojay_exit", void_, {i32_}, true);
    rt_args_ = rt("__ojay_command_line_arguments", ptr_, {ptr_});
    rt_array_reserve_ = rt("__ojay_array_reserve", void_, {ptr_, i64_, i64_, i32_, ptr_, ptr_, i64_, ptr_});
    rt_init_ = rt("__ojay_runtime_init", void_, {i32_, ptr_});
    rt_cstring_ = rt("__ojay_to_c_string", ptr_, {ptr_, i64_, ptr_, ptr_});
  }

  // ------------------------------------------------------------------ constants

  llvm::GlobalVariable* cstring_global(const std::string& s) {
    auto it = strings_.find(s);
    if (it != strings_.end()) return it->second;
    auto* init = llvm::ConstantDataArray::getString(ctx_, s, /*AddNull=*/true);
    auto* gv = new llvm::GlobalVariable(*mod_, init->getType(), true, llvm::GlobalValue::PrivateLinkage, init, ".str");
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    gv->setAlignment(llvm::Align(1));
    strings_[s] = gv;
    return gv;
  }

  llvm::Constant* string_const(const std::string& s) {
    return llvm::ConstantStruct::get(llvm::cast<llvm::StructType>(lt(T.string_t)),
                                     {llvm::ConstantInt::get(i64_, s.size()), cstring_global(s)});
  }

  llvm::Constant* const_value(const ConstValue& cv, Type* t) {
    switch (cv.kind) {
      case ConstValue::Int:
        if (t->kind == TypeKind::Float) return llvm::ConstantFP::get(lt(t), (double)cv.i);
        if (t->kind == TypeKind::Bool) return llvm::ConstantInt::get(i1_, cv.i != 0);
        return llvm::ConstantInt::get(lt(t), (uint64_t)cv.i, true);
      case ConstValue::Float:
        if (t->kind == TypeKind::Int) return llvm::ConstantInt::get(lt(t), (uint64_t)(int64_t)cv.f, true);
        return llvm::ConstantFP::get(lt(t), cv.f);
      case ConstValue::Bool:
        return llvm::ConstantInt::get(i1_, cv.b);
      case ConstValue::String:
        return string_const(cv.s);
      case ConstValue::Null:
        return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));
      case ConstValue::Address:
        if (t->kind == TypeKind::Int) return llvm::ConstantInt::get(lt(t), (uint64_t)cv.i);
        return llvm::ConstantExpr::getIntToPtr(llvm::ConstantInt::get(dl_.getIntPtrType(ctx_), (uint64_t)cv.i),
                                               llvm::PointerType::get(ctx_, 0));
      case ConstValue::TypeV:
        return type_info(cv.t);
      case ConstValue::Code:
        return llvm::ConstantInt::get(i64_, (uint64_t)cv.i);
      case ConstValue::Proc:
        if (!cv.proc->llvm_fn)
          throw CompileError(cv.proc->loc, "internal error: procedure of a #run result has no code");
        return cv.proc->llvm_fn;
      case ConstValue::Pointer: {  // into a baked object: one (writable) global per object
        auto it = baked_objects_.find(cv.elems.get());
        llvm::GlobalVariable* gv;
        if (it != baked_objects_.end()) {
          gv = it->second;
        } else {
          // The global exists before its initializer is made, so pointers
          // back to it (a cycle) refer to it. If the initializer's LLVM type
          // differs (a constant of the same layout), a global of that type
          // replaces it.
          gv = new llvm::GlobalVariable(*mod_, lt(cv.t), false, llvm::GlobalValue::PrivateLinkage, nullptr, "baked");
          gv->setAlignment(llvm::Align(align_of(cv.t)));
          baked_objects_[cv.elems.get()] = gv;
          llvm::Constant* init = const_value((*cv.elems)[0], cv.t);
          if (init->getType() != gv->getValueType()) {
            auto* ng = new llvm::GlobalVariable(*mod_, init->getType(), false, llvm::GlobalValue::PrivateLinkage,
                                                init, "baked");
            ng->setAlignment(gv->getAlign());
            gv->replaceAllUsesWith(ng);
            gv->eraseFromParent();
            gv = ng;
            baked_objects_[cv.elems.get()] = gv;
          } else {
            gv->setInitializer(init);
          }
        }
        if (cv.i == 0) return gv;
        return llvm::ConstantExpr::getInBoundsGetElementPtr(i8_, gv, llvm::ConstantInt::get(i64_, (uint64_t)cv.i));
      }
      case ConstValue::Aggregate: {
        const auto& es = *cv.elems;
        if (es.empty()) return llvm::Constant::getNullValue(lt(t));  // zero value (an empty array view)
        if (t->kind == TypeKind::Array && !t->is_fixed_array()) {  // count, data (a dynamic array owns no memory)
          auto* st = llvm::cast<llvm::StructType>(lt(t));
          std::vector<llvm::Constant*> fs;
          for (unsigned k = 0; k < st->getNumElements(); ++k) fs.push_back(llvm::Constant::getNullValue(st->getElementType(k)));
          fs[0] = const_value(es[0], T.s64);
          fs[1] = const_value(es[1], T.pointer_to(t->elem));
          return llvm::ConstantStruct::get(st, fs);
        }
        if (t->kind == TypeKind::Any)
          return llvm::ConstantStruct::get(llvm::cast<llvm::StructType>(lt(t)),
                                           {const_value(es[0], T.type_t), const_value(es[1], T.void_ptr)});
        if (t->is_union) {  // (member index, its value): the member's bytes, padded to the union's size
          Type* mt = t->fields[(size_t)es[0].i].type;
          llvm::Constant* m = const_value(es[1], mt);
          auto* ut = llvm::cast<llvm::StructType>(lt(t));
          if (ut->getNumElements() >= 1 && ut->getElementType(0) == m->getType()) {
            std::vector<llvm::Constant*> fs{m};
            for (unsigned k = 1; k < ut->getNumElements(); ++k) fs.push_back(llvm::Constant::getNullValue(ut->getElementType(k)));
            return llvm::ConstantStruct::get(ut, fs);
          }
          int64_t pad = (int64_t)size_of(t) - (int64_t)dl_.getTypeAllocSize(m->getType());
          if (pad <= 0) return llvm::ConstantStruct::getAnon(ctx_, {m}, /*Packed=*/true);
          return llvm::ConstantStruct::getAnon(ctx_, {m, llvm::ConstantAggregateZero::get(llvm::ArrayType::get(i8_, pad))},
                                               /*Packed=*/true);
        }
        if (t->is_fixed_array()) {
          std::vector<llvm::Constant*> elems;
          elems.reserve(es.size());
          for (const ConstValue& e : es) elems.push_back(const_value(e, t->elem));
          return array_const(t, elems);
        }
        if (t->kind == TypeKind::Struct && !t->is_union && es.size() == t->fields.size()) {
          std::vector<llvm::Constant*> fields;
          for (size_t i = 0; i < es.size(); ++i)
            fields.push_back(t->fields[i].type->kind == TypeKind::Void ? nullptr : const_value(es[i], t->fields[i].type));
          return struct_const(t, fields, /*consistent=*/true);
        }
        break;
      }
      case ConstValue::None:
        break;
    }
    throw CompileError(SourceLoc{}, "internal error: const_value of non-constant");
  }

  // A constant fixed array. Elements whose constant has another LLVM type than
  // the element type (a struct constant with overlapping members, see
  // struct_const) make an equivalent literal struct of the same layout.
  llvm::Constant* array_const(Type* t, const std::vector<llvm::Constant*>& elems) {
    llvm::Type* et = lt(t->elem);
    bool same = true;
    for (llvm::Constant* c : elems) same &= c->getType() == et;
    if (same) return llvm::ConstantArray::get(llvm::cast<llvm::ArrayType>(lt(t)), elems);
    return llvm::ConstantStruct::getAnon(ctx_, elems, /*Packed=*/true);
  }

  // A constant of struct t (not a union) from one constant per field (null:
  // zero). Normally a constant of lt(t). When a member that is not an element
  // of lt(t) (it overlaps another, #place) has a value, or a value's LLVM type
  // differs from the element's, the result is a packed literal struct with the
  // same bytes: the values at their offsets, zero bytes between them. Values
  // of overlapping members cannot both be kept; sema allows that only for
  // `consistent` ones (#run results, whose bytes agree), where the larger is kept.
  llvm::Constant* struct_const(Type* t, const std::vector<llvm::Constant*>& vals, bool consistent = false) {
    const StructLayout& L = layout(t);
    auto* st = llvm::cast<llvm::StructType>(lt(t));
    bool fits = true;
    for (size_t i = 0; i < vals.size(); ++i) {
      if (!vals[i]) continue;
      int el = L.simple ? (int)i : L.elem[i];
      if (el < 0) {
        if (!vals[i]->isNullValue()) fits = false;
      } else if (vals[i]->getType() != st->getElementType((unsigned)el)) {
        fits = false;
      }
    }
    if (fits) {
      std::vector<llvm::Constant*> es(st->getNumElements(), nullptr);
      for (size_t i = 0; i < vals.size(); ++i) {
        int el = L.simple ? (int)i : L.elem[i];
        if (el >= 0 && vals[i]) es[(size_t)el] = vals[i];
      }
      for (unsigned k = 0; k < es.size(); ++k)
        if (!es[k]) es[k] = llvm::Constant::getNullValue(st->getElementType(k));
      return llvm::ConstantStruct::get(st, es);
    }
    // Pieces at their offsets.
    std::vector<size_t> order;
    for (size_t i = 0; i < vals.size(); ++i)
      if (vals[i] && !vals[i]->isNullValue() && t->fields[i].type->size > 0) order.push_back(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return t->fields[a].type->size > t->fields[b].type->size; });
    std::vector<size_t> placed;
    for (size_t i : order) {
      bool clash = false;
      for (size_t j : placed) clash |= fields_overlap(t, i, j);
      if (!clash) placed.push_back(i);
      else if (!consistent)
        throw CompileError(SourceLoc{}, cat("internal error: overlapping values in a constant of ", type_name(t)));
    }
    std::sort(placed.begin(), placed.end(),
              [&](size_t a, size_t b) { return t->fields[a].offset < t->fields[b].offset; });
    std::vector<llvm::Constant*> es;
    uint64_t cur = 0;
    for (size_t i : placed) {
      uint64_t off = (uint64_t)t->fields[i].offset;
      if (off > cur) es.push_back(llvm::Constant::getNullValue(llvm::ArrayType::get(i8_, off - cur)));
      es.push_back(vals[i]);
      cur = off + dl_.getTypeAllocSize(vals[i]->getType());
    }
    if ((uint64_t)t->size > cur) es.push_back(llvm::Constant::getNullValue(llvm::ArrayType::get(i8_, t->size - cur)));
    return llvm::ConstantStruct::getAnon(ctx_, es, /*Packed=*/true);
  }

  // A constant as a value of type t (an SSA value of lt(t)).
  llvm::Value* const_rvalue(const ConstValue& cv, Type* t) {
    llvm::Constant* c = const_value(cv, t);
    if (c->getType() == lt(t)) return c;
    return b_.CreateLoad(lt(t), const_global(cv, t));
  }

  // A read-only global holding an aggregate constant, for code that needs its
  // address (indexing a table made by #run with a runtime index, iterating it).
  llvm::Constant* const_global(const ConstValue& cv, Type* t) {
    auto key = std::make_pair(static_cast<const void*>(cv.elems.get()), t);
    auto it = const_globals_.find(key);
    if (it != const_globals_.end()) return it->second;
    llvm::Constant* c = const_value(cv, t);
    auto* gv = new llvm::GlobalVariable(*mod_, c->getType(), true, llvm::GlobalValue::PrivateLinkage, c, ".const");
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    gv->setAlignment(llvm::Align(align_of(t)));
    const_globals_[key] = gv;
    return gv;
  }

  // The default (initial) value of a type, honoring struct member defaults.
  llvm::Constant* default_value(Type* t) {
    auto it = defaults_.find(t);
    if (it != defaults_.end()) return it->second;
    llvm::Constant* c = nullptr;
    if (t->kind == TypeKind::Struct && !t->is_union) {
      c = struct_const(t, field_defaults(t, {}));
      if (c->isNullValue()) c = llvm::Constant::getNullValue(lt(t));
    } else if (t->is_fixed_array()) {
      llvm::Constant* e = default_value(t->elem);
      if (e->isNullValue()) {
        c = llvm::Constant::getNullValue(lt(t));
      } else {
        std::vector<llvm::Constant*> elems(t->count, e);
        c = array_const(t, elems);
      }
    } else {
      c = llvm::Constant::getNullValue(lt(t));
    }
    defaults_[t] = c;
    return c;
  }

  // The initial value of each field of struct t (null: zero): its default, or
  // its type's, except for members overlapping one that `given` sets (#place).
  std::vector<llvm::Constant*> field_defaults(Type* t, const std::vector<Expr*>& given) {
    std::vector<llvm::Constant*> out(t->fields.size(), nullptr);
    for (size_t i = 0; i < t->fields.size(); ++i) {
      const StructField& f = t->fields[i];
      if (f.uninit || f.type->kind == TypeKind::Void) continue;
      bool overridden = false;
      for (size_t j = 0; j < given.size() && t->has_place; ++j)
        overridden |= j != i && given[j] && fields_overlap(t, i, j);
      if (overridden) continue;
      llvm::Constant* fc = f.default_value ? const_init(f.default_value, f.type) : default_value(f.type);
      if (!fc->isNullValue() || !t->has_place) out[i] = fc;
    }
    return out;
  }

  // Constant initializer for a statically-initializable expression.
  llvm::Constant* const_init(Expr* e, Type* t) {
    if (e->is_const()) return const_value(e->cv, e->type);
    switch (e->kind) {
      case NK::StructLit: {
        auto* s = static_cast<StructLit*>(e);
        std::vector<llvm::Constant*> fields = field_defaults(s->type, s->field_values);
        for (size_t i = 0; i < s->field_values.size(); ++i)
          if (s->field_values[i]) fields[i] = const_init(s->field_values[i], s->type->fields[i].type);
        return struct_const(s->type, fields);
      }
      case NK::ArrayLit: {
        auto* a = static_cast<ArrayLit*>(e);
        std::vector<llvm::Constant*> elems;
        for (Expr* x : a->elems) elems.push_back(const_init(x, a->type->elem));
        return array_const(a->type, elems);
      }
      case NK::ProcLit:
        return static_cast<ProcLit*>(e)->llvm_fn;
      case NK::Ident:
        return static_cast<Ident*>(e)->sym->proc->llvm_fn;
      case NK::Unary:  // the address of a global variable
        return llvm::cast<llvm::Constant>(static_cast<Ident*>(static_cast<Unary*>(e)->operand)->sym->llvm_value);
      case NK::ImplicitConv: {
        auto* ic = static_cast<ImplicitConv*>(e);
        Expr* op = ic->operand;
        if (op->is_const()) {
          if (op->cv.kind == ConstValue::Int || op->cv.kind == ConstValue::Float)
            return const_value(op->cv, ic->type);
          return const_value(op->cv, op->type);  // pointer conversions are no-ops
        }
        return const_init(op, op->type);
      }
      default:
        break;
    }
    (void)t;
    throw CompileError(e->loc, "internal error: expression is not a static initializer");
  }

  // ------------------------------------------------------------------ type info

  // ---- runtime type information
  //
  // Each type gets one constant global holding its Type_Info as the
  // Type_Info_* struct for its kind (core.jai); a Type value points to it.

  using TiFields = std::map<std::string, llvm::Constant*>;

  // A constant of Type_Info_* struct `st` with the given members (by name;
  // the others are zero). The `using info: Type_Info` member takes `type` and
  // `runtime_size` from the same map.
  llvm::Constant* ti_const(Type* st, const TiFields& vals) {
    std::vector<llvm::Constant*> fields;
    for (const StructField& f : st->fields) {
      if (f.is_using && f.type == prog_.type_info.info) {
        fields.push_back(ti_const(f.type, vals));
        continue;
      }
      auto it = vals.find(f.name);
      llvm::Constant* v = it != vals.end() ? it->second : llvm::Constant::getNullValue(lt(f.type));
      llvm::Type* ft = lt(f.type);
      if (f.type->kind == TypeKind::Int || f.type->kind == TypeKind::Enum) {
        if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(v)) v = llvm::ConstantInt::get(ft, ci->getZExtValue());
      } else if (v->getType() != ft && ft->isStructTy()) {  // a view or string built with a literal struct type
        auto* fst = llvm::cast<llvm::StructType>(ft);
        std::vector<llvm::Constant*> es;
        for (unsigned k = 0; k < fst->getNumElements(); ++k) es.push_back(v->getAggregateElement(k));
        v = llvm::ConstantStruct::get(fst, es);
      }
      fields.push_back(v);
    }
    return llvm::ConstantStruct::get(llvm::cast<llvm::StructType>(lt(st)), fields);
  }

  // A constant view ({count, data}) of `elems`, of element type `elem_t`.
  llvm::Constant* ti_view(llvm::Type* elem_t, const std::vector<llvm::Constant*>& elems, const std::string& name) {
    llvm::Constant* data = llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));
    if (!elems.empty()) {
      auto* at = llvm::ArrayType::get(elem_t, elems.size());
      data = new llvm::GlobalVariable(*mod_, at, true, llvm::GlobalValue::PrivateLinkage,
                                      llvm::ConstantArray::get(at, elems), name);
    }
    auto* view_t = llvm::StructType::get(ctx_, {i64_, ptr_});
    return llvm::ConstantStruct::get(view_t, {llvm::ConstantInt::get(i64_, elems.size()), data});
  }

  llvm::Constant* type_info(Type* t) {
    if (t->type_info) return t->type_info;
    const auto& ti = prog_.type_info;
    Type* st = ti.info;
    int64_t tag = TI_VOID;
    if (t->distinct_of) {  // Type_Info_Variant: the name, and the base's Type_Info
      std::string name = type_name(t);
      auto* gv = new llvm::GlobalVariable(*mod_, lt(ti.variant), true, llvm::GlobalValue::PrivateLinkage, nullptr,
                                          "__type_info." + name);
      t->type_info = gv;
      TiFields v;
      v["type"] = llvm::ConstantInt::get(i64_, TI_VARIANT);
      v["runtime_size"] = llvm::ConstantInt::get(i64_, size_of(t));
      v["name"] = string_const(name);
      v["variant_of"] = type_info(t->distinct_of);
      v["variant_flags"] = llvm::ConstantInt::get(i32_, t->isa ? 1 : 0);
      gv->setInitializer(ti_const(ti.variant, v));
      return gv;
    }
    switch (t->kind) {
      case TypeKind::Int: tag = TI_INTEGER; st = ti.integer; break;
      case TypeKind::Float: tag = TI_FLOAT; st = ti.float_; break;
      case TypeKind::Bool: tag = TI_BOOL; break;
      case TypeKind::String: tag = TI_STRING; break;
      case TypeKind::Pointer: case TypeKind::Null: tag = TI_POINTER; st = ti.pointer; break;
      case TypeKind::Proc: tag = TI_PROCEDURE; st = ti.procedure; break;
      case TypeKind::Struct: tag = TI_STRUCT; st = ti.struct_; break;
      case TypeKind::Array: tag = TI_ARRAY; st = ti.array; break;
      case TypeKind::Any: tag = TI_ANY; break;
      case TypeKind::Enum: tag = TI_ENUM; st = ti.enum_; break;
      case TypeKind::TypeT: tag = TI_TYPE; break;
      case TypeKind::Code: tag = TI_CODE; break;
      default: tag = TI_VOID; break;
    }
    std::string name = type_name(t);
    // Created before its members, which may refer back to it.
    auto* gv = new llvm::GlobalVariable(*mod_, lt(st), true, llvm::GlobalValue::PrivateLinkage, nullptr,
                                        "__type_info." + name);
    t->type_info = gv;
    int64_t size = (t->kind == TypeKind::Void || t->kind == TypeKind::Tuple) ? 0 : (int64_t)size_of(t);
    TiFields v;
    v["type"] = llvm::ConstantInt::get(i64_, tag);
    v["runtime_size"] = llvm::ConstantInt::get(i64_, size);
    auto i32c = [&](uint64_t x) { return llvm::ConstantInt::get(i32_, x); };
    switch (t->kind) {
      case TypeKind::Int:
        v["signed"] = llvm::ConstantInt::get(i1_, t->is_signed);
        break;
      case TypeKind::Pointer:
        v["pointer_to"] = type_info(t->elem);
        break;
      case TypeKind::Null:
        v["pointer_to"] = type_info(T.void_t);
        break;
      case TypeKind::Array:
        v["element_type"] = type_info(t->elem);
        v["array_type"] = i32c(t->array_kind == ArrayKind::Fixed ? 0 : t->array_kind == ArrayKind::View ? 1 : 2);
        v["array_count"] = llvm::ConstantInt::get(i64_, t->array_kind == ArrayKind::Fixed ? (uint64_t)t->count : (uint64_t)-1);
        break;
      case TypeKind::Enum: {
        v["name"] = string_const(name);
        v["internal_type"] = type_info(t->elem);
        v["enum_type_flags"] = i32c(t->is_flags ? 1 : 0);
        std::vector<llvm::Constant*> names, values;
        for (auto& m : t->members) {
          names.push_back(string_const(m.name));
          values.push_back(llvm::ConstantInt::get(i64_, (uint64_t)m.value, true));
        }
        v["names"] = ti_view(lt(T.string_t), names, "__type_info_names." + name);
        v["values"] = ti_view(i64_, values, "__type_info_values." + name);
        break;
      }
      case TypeKind::Struct: {
        v["name"] = string_const(name);
        v["textual_flags"] = i32c((t->is_union ? 0x2 : 0) | (t->has_align ? 0x10 : 0));
        std::vector<llvm::Constant*> ms;
        size_t next_const = 0;
        auto add_constants = [&](size_t before) {  // the constants declared before field `before`
          for (; next_const < t->constants.size() && t->constants[next_const].field_index <= before; ++next_const) {
            const StructConstant& k = t->constants[next_const];
            TiFields mv;
            mv["name"] = string_const(k.name);
            mv["type"] = type_info(k.type);
            mv["offset_in_bytes"] = llvm::ConstantInt::get(i64_, 0);
            mv["flags"] = i32c(0x1 | (k.type->is_proc() ? 0x8 : 0));  // CONSTANT
            ms.push_back(ti_const(prog_.type_info.member, mv));
          }
        };
        for (size_t i = 0; i < t->fields.size(); ++i) {
          add_constants(i);
          const StructField& f = t->fields[i];
          TiFields mv;
          mv["name"] = string_const(f.name);
          mv["type"] = type_info(f.type);
          mv["offset_in_bytes"] = llvm::ConstantInt::get(i64_, (uint64_t)f.offset);
          uint32_t flags = (f.is_using ? 0x4 : 0) | (f.anonymous ? 0x100 : 0) | (f.type->is_proc() ? 0x8 : 0);
          mv["flags"] = i32c(flags);
          if (!f.notes.empty()) {
            std::vector<llvm::Constant*> ns;
            for (auto& n : f.notes) ns.push_back(string_const(n));
            mv["notes"] = ti_view(lt(T.string_t), ns, "__type_info_notes." + name + "." + f.name);
          }
          ms.push_back(ti_const(prog_.type_info.member, mv));
        }
        add_constants(SIZE_MAX);
        v["members"] = ti_view(lt(prog_.type_info.member), ms, "__type_info_members." + name);
        if (!t->struct_notes.empty()) {
          std::vector<llvm::Constant*> ns;
          for (auto& n : t->struct_notes) ns.push_back(string_const(n));
          v["notes"] = ti_view(lt(T.string_t), ns, "__type_info_struct_notes." + name);
        }
        if (!t->is_union) {
          llvm::Constant* def = default_value(t);
          if (!def->isNullValue()) v["initializer"] = initializer_fn(t, def, name);
        }
        if (t->poly_source) {  // an instance of a polymorphic struct: its source and parameters
          v["polymorph_source_struct"] = type_info(t->poly_source);
          // The values, laid out as an LLVM struct of their types (natural alignment).
          std::vector<llvm::Type*> vts;
          std::vector<llvm::Constant*> vals;
          for (auto& pp : t->poly_params) {
            vts.push_back(lt(pp.type));
            vals.push_back(const_value(*pp.value, pp.type));
          }
          auto* storage_t = llvm::StructType::get(ctx_, vts);
          const llvm::StructLayout* sl = dl_.getStructLayout(storage_t);
          std::vector<llvm::Constant*> ps;
          for (size_t i = 0; i < t->poly_params.size(); ++i) {
            TiFields pv;
            pv["name"] = string_const(t->poly_params[i].name);
            pv["type"] = type_info(t->poly_params[i].type);
            pv["offset_in_bytes"] = llvm::ConstantInt::get(i64_, sl->getElementOffset((unsigned)i));
            ps.push_back(ti_const(prog_.type_info.member, pv));
          }
          v["specified_parameters"] = ti_view(lt(prog_.type_info.member), ps, "__type_info_params." + name);
          auto* storage = new llvm::GlobalVariable(*mod_, storage_t, true, llvm::GlobalValue::PrivateLinkage,
                                                   llvm::ConstantStruct::get(storage_t, vals),
                                                   "__type_info_constants." + name);
          v["constant_storage"] = llvm::ConstantStruct::get(
              llvm::StructType::get(ctx_, {i64_, ptr_}), {llvm::ConstantInt::get(i64_, sl->getSizeInBytes()), storage});
        }
        break;
      }
      case TypeKind::Proc: {
        std::vector<llvm::Constant*> args, rets;
        for (Type* p : t->params) args.push_back(type_info(p));
        for (Type* r : t->returns) rets.push_back(type_info(r));
        v["argument_types"] = ti_view(ptr_, args, "__type_info_args." + name);
        v["return_types"] = ti_view(ptr_, rets, "__type_info_returns." + name);
        v["procedure_flags"] = i32c((t->c_call ? 1 : 0) | (t->variadic ? 2 : 0));
        break;
      }
      default:
        break;
    }
    gv->setInitializer(ti_const(st, v));
    return gv;
  }

  // ------------------------------------------------------------------ declarations

  void declare_proc(ProcLit* p) {
    llvm::FunctionType* ft = proc_fn_type(p->type, p->loc);
    if (p->is_foreign) {
      if (llvm::Function* existing = mod_->getFunction(p->foreign_name)) {
        if (existing->getFunctionType() != ft)
          throw CompileError(p->loc, cat("#foreign procedure '", p->foreign_name,
                                         "' is declared twice with different signatures"));
        p->llvm_fn = existing;
        return;
      }
      p->llvm_fn = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, p->foreign_name, mod_.get());
      p->llvm_fn->setAttributes(c_attrs(p->type));
      p->llvm_fn->addFnAttr(llvm::Attribute::NoUnwind);
      return;
    }
    std::string name = p->name.empty() ? "__anonymous_proc" : p->name;
    if (p == prog_.main_proc) name = "__ojay_user_main";
    auto linkage = llvm::GlobalValue::InternalLinkage;
    if (p->program_export) {
      name = p->export_name.empty() ? p->name : p->export_name;
      if (mod_->getNamedValue(name))
        throw CompileError(p->loc, cat("#program_export: the symbol '", name, "' is already defined"));
      linkage = llvm::GlobalValue::ExternalLinkage;
    }
    p->llvm_fn = llvm::Function::Create(ft, linkage, name, mod_.get());
    if (p->type->c_call) p->llvm_fn->setAttributes(c_attrs(p->type));
    p->llvm_fn->addFnAttr(llvm::Attribute::NoUnwind);
    if (p->always_inline && !p->program_export) p->llvm_fn->addFnAttr(llvm::Attribute::AlwaysInline);
  }

  void declare_global(Symbol* g) {
    auto* gv = new llvm::GlobalVariable(*mod_, lt(g->type), false, llvm::GlobalValue::InternalLinkage, nullptr, g->name);
    gv->setAlignment(llvm::Align(std::max<uint64_t>(align_of(g->type), (uint64_t)g->align)));
    g->llvm_value = gv;
  }

  void init_global(Symbol* g) {
    llvm::Constant* init = g->init ? const_init(g->init, g->type) : default_value(g->type);
    if (g->decl && g->decl->uninit) init = llvm::Constant::getNullValue(lt(g->type));
    auto* gv = llvm::cast<llvm::GlobalVariable>(g->llvm_value);
    if (init->getType() != gv->getValueType()) {
      // A constant of the same layout but another LLVM type (struct_const):
      // the global takes its type; uses only need its address.
      auto* ng = new llvm::GlobalVariable(*mod_, init->getType(), false, gv->getLinkage(), init, "", gv);
      ng->setAlignment(gv->getAlign());
      ng->takeName(gv);
      gv->replaceAllUsesWith(ng);
      gv->eraseFromParent();
      g->llvm_value = ng;
      return;
    }
    gv->setInitializer(init);
  }

  void emit_context_pointer() {
    context_ptr_ = new llvm::GlobalVariable(*mod_, ptr_, false, llvm::GlobalValue::InternalLinkage,
                                            llvm::cast<llvm::Constant>(prog_.default_context->llvm_value),
                                            "__ojay_context");
    // With threads each thread has its own current context (and starts with
    // the default one): thread-local on an OS; on bare metal the runtime's
    // task switch (__ojay_task_switch) saves and restores it per task.
    if (prog_.uses_threads) {
      if (llvm::Triple(mod_->getTargetTriple()).getOS() == llvm::Triple::UnknownOS) {
        context_ptr_->setLinkage(llvm::GlobalValue::ExternalLinkage);
        context_ptr_->setVisibility(llvm::GlobalValue::HiddenVisibility);
      } else {
        context_ptr_->setThreadLocal(true);
      }
    }
  }

  // The address of the current Context (its allocator is its first member).
  llvm::Value* context_addr() { return b_.CreateLoad(ptr_, context_ptr_, "context"); }

  // Records `#system_library` dependencies in the object file
  // (.deplibs section); ld.lld links them automatically, other linkers need
  // them on the command line.
  void emit_dependent_libraries() {
    llvm::NamedMDNode* md = nullptr;
    for (const std::string& lib : prog_.system_libraries) {
      std::string n = lib.rfind("lib", 0) == 0 ? lib.substr(3) : lib;
      if (n == "c" || n == "crt") continue;  // linked by default
      if (!md) md = mod_->getOrInsertNamedMetadata("llvm.dependent-libraries");
      md->addOperand(llvm::MDNode::get(ctx_, {llvm::MDString::get(ctx_, n)}));
    }
  }

  void emit_c_main() {
    auto* ft = llvm::FunctionType::get(i32_, {i32_, ptr_}, false);
    auto* f = llvm::Function::Create(ft, llvm::GlobalValue::ExternalLinkage, "main", mod_.get());
    f->addFnAttr(llvm::Attribute::NoUnwind);
    auto* bb = llvm::BasicBlock::Create(ctx_, "entry", f);
    b_.SetInsertPoint(bb);
    b_.CreateCall(rt_init_, {f->getArg(0), f->getArg(1)});
    b_.CreateCall(prog_.main_proc->llvm_fn->getFunctionType(), prog_.main_proc->llvm_fn, {});
    b_.CreateRet(llvm::ConstantInt::get(i32_, 0));
  }

  // ------------------------------------------------------------------ procedure bodies

  llvm::AllocaInst* make_alloca(Type* t, const std::string& name = "") {
    llvm::IRBuilder<> eb(entry_, entry_->begin());
    auto* a = eb.CreateAlloca(lt(t), nullptr, name);
    a->setAlignment(llvm::Align(align_of(t)));
    return a;
  }

  llvm::AllocaInst* make_alloca_raw(llvm::Type* ty, const std::string& name = "") {
    llvm::IRBuilder<> eb(entry_, entry_->begin());
    return eb.CreateAlloca(ty, nullptr, name);
  }

  bool terminated() {
    llvm::BasicBlock* bb = b_.GetInsertBlock();
    return !bb->empty() && bb->back().isTerminator();
  }

  // After a terminator, continue emitting into an unreachable block.
  void start_dead_block() {
    auto* bb = llvm::BasicBlock::Create(ctx_, "dead", fn_);
    b_.SetInsertPoint(bb);
  }

  void emit_proc_body(ProcLit* p) {
    fn_ = p->llvm_fn;
    proc_ = p;
    entry_ = llvm::BasicBlock::Create(ctx_, "entry", fn_);
    b_.SetInsertPoint(entry_);
    scopes_.clear();
    scopes_.push_back({});
    if (p->type->c_call) c_prologue(p);
    else for (size_t i = 0; i < p->params.size(); ++i) {
      Symbol* s = p->params[i]->syms[0];
      auto* a = make_alloca(s->type, s->name);
      b_.CreateStore(fn_->getArg(i), a);
      s->llvm_value = a;
    }
    emit_stmts(p->body->stmts);
    if (!terminated()) {
      emit_defers_down_to(0);
      if (p->type->returns.empty()) b_.CreateRetVoid();
      else b_.CreateUnreachable();
    }
    scopes_.clear();
    // Remove unreachable "dead" blocks with no predecessors.
    std::vector<llvm::BasicBlock*> dead;
    for (auto& bb : *fn_)
      if (&bb != &fn_->getEntryBlock() && llvm::pred_empty(&bb)) dead.push_back(&bb);
    for (auto* bb : dead) {
      if (bb->empty() || !bb->back().isTerminator()) {
        llvm::IRBuilder<> tb(bb);
        tb.CreateUnreachable();
      }
    }
  }

  void emit_stmts(const std::vector<Stmt*>& stmts) {
    for (Stmt* s : stmts) {
      if (terminated()) start_dead_block();
      emit_stmt(s);
    }
  }

  // Runs (emits) the defers of scopes_[from..end) in reverse order, without popping.
  // A push_context scope also restores the previous context when left.
  void emit_defers_down_to(size_t from) {
    for (size_t i = scopes_.size(); i-- > from;) {
      std::vector<Stmt*> ds = scopes_[i].defers;
      for (size_t j = ds.size(); j-- > 0;) {
        if (terminated()) start_dead_block();
        emit_body(ds[j]);
      }
      if (llvm::Value* old = scopes_[i].restore_context) {
        if (terminated()) start_dead_block();
        b_.CreateStore(old, context_ptr_);
      }
    }
  }

  void emit_push_context(PushContext* pc) {
    // The block runs with a copy of the context it was given.
    llvm::Value* ctx = make_alloca(pc->ctx->type, "pushed.context");
    b_.CreateStore(rvalue(pc->ctx), ctx);
    CgScope sc;
    sc.restore_context = b_.CreateLoad(ptr_, context_ptr_, "context.saved");
    b_.CreateStore(ctx, context_ptr_);
    scopes_.push_back(sc);
    emit_scoped(pc->body->stmts);
    if (!terminated()) emit_defers_down_to(scopes_.size() - 1);
    scopes_.pop_back();
  }

  void emit_scoped(const std::vector<Stmt*>& stmts) {
    scopes_.push_back({});
    emit_stmts(stmts);
    if (!terminated()) emit_defers_down_to(scopes_.size() - 1);
    scopes_.pop_back();
  }

  void emit_body(Stmt* s) {
    if (s->kind == NK::Block) {
      emit_scoped(static_cast<Block*>(s)->stmts);
    } else {
      emit_scoped({s});
    }
  }

  void emit_stmt(Stmt* s) {
    switch (s->kind) {
      case NK::Block: emit_scoped(static_cast<Block*>(s)->stmts); return;
      case NK::ExprStmt: {
        Expr* e = static_cast<ExprStmt*>(s)->expr;
        if (e->kind == NK::Directive) return;  // #assert, checked at compile time
        emit_expr_any(e);
        return;
      }
      case NK::Decl: emit_local_decl(static_cast<Decl*>(s)); return;
      case NK::Using: return;  // resolved by sema
      case NK::Assign: emit_assign(static_cast<Assign*>(s)); return;
      case NK::If: emit_if(static_cast<If*>(s)); return;
      case NK::Switch: emit_switch(static_cast<Switch*>(s)); return;
      case NK::While: emit_while(static_cast<While*>(s)); return;
      case NK::For: emit_for(static_cast<For*>(s)); return;
      case NK::Remove: emit_remove(static_cast<Remove*>(s)); return;
      case NK::Return: {
        auto* r = static_cast<Return*>(s);
        if (r->expand_target) emit_macro_return(r);
        else emit_return(r);
        return;
      }
      case NK::Insert:  // inserted code runs in place: it opens no scope
        emit_stmts(static_cast<Insert*>(s)->result->stmts);
        return;
      case NK::Break:
      case NK::Continue: {
        auto* bc = static_cast<BreakContinue*>(s);
        if (bc->leave) {  // `break x` of a for_expansion loop: leaves its expansion
          size_t e = expansion_scope(bc->leave);
          emit_defers_down_to(e);
          if (!terminated()) b_.CreateBr(scopes_[e].break_bb);
          return;
        }
        size_t idx = scopes_.size();
        while (idx-- > 0)
          if (scopes_[idx].loop == bc->target) break;
        emit_defers_down_to(idx + 1);
        if (terminated()) return;
        b_.CreateBr(s->kind == NK::Break ? scopes_[idx].break_bb : scopes_[idx].continue_bb);
        return;
      }
      case NK::Defer: {
        auto* d = static_cast<Defer*>(s);
        // `defer in a macro: runs when the scope the macro was called from ends.
        if (d->backtick) scopes_[expansion_scope(d->target) - 1].defers.push_back(d->body);
        else scopes_.back().defers.push_back(d->body);
        return;
      }
      case NK::StaticIf: {
        auto* si = static_cast<StaticIf*>(s);
        emit_stmts(si->taken == 1 ? si->then_s : si->else_s);
        return;
      }
      case NK::PushContext: emit_push_context(static_cast<PushContext*>(s)); return;
      case NK::Asm: emit_asm(static_cast<AsmStmt*>(s)); return;
      default:
        throw CompileError(s->loc, "internal error: unexpected statement in codegen");
    }
  }

  // The index in scopes_ of the top-level scope of expansion `x`.
  size_t expansion_scope(MacroExpansion* x) {
    for (size_t i = scopes_.size(); i-- > 0;)
      if (scopes_[i].expansion == x) return i;
    throw CompileError(x->loc, "internal error: macro expansion not found");
  }

  // An inlined #expand macro: its statements, then its value (null if void).
  llvm::Value* emit_expansion(MacroExpansion* x) {
    CgScope sc;
    sc.expansion = x;
    sc.break_bb = llvm::BasicBlock::Create(ctx_, "macro.end");
    if (x->type->kind != TypeKind::Void) sc.result = make_alloca(x->type, "macro.result");
    llvm::BasicBlock* end_bb = sc.break_bb;
    llvm::AllocaInst* result = sc.result;
    scopes_.push_back(sc);
    emit_stmts(x->body->stmts);
    if (!terminated()) {
      emit_defers_down_to(scopes_.size() - 1);
      if (!terminated()) b_.CreateBr(end_bb);
    }
    scopes_.pop_back();
    end_bb->insertInto(fn_);
    b_.SetInsertPoint(end_bb);
    return result ? b_.CreateLoad(lt(x->type), result) : nullptr;
  }

  // A plain `return` in a macro body: leaves the expansion with its value.
  void emit_macro_return(Return* r) {
    size_t idx = expansion_scope(r->expand_target);
    if (!r->values.empty()) b_.CreateStore(return_value(r, r->expand_target->macro->type->returns), scopes_[idx].result);
    emit_defers_down_to(idx);
    if (terminated()) return;
    b_.CreateBr(scopes_[idx].break_bb);
  }

  // Evaluates an expression whose result may be void or unused.
  void emit_expr_any(Expr* e) {
    if (e->is_const()) return;
    if (e->kind == NK::Expansion) {
      emit_expansion(static_cast<MacroExpansion*>(e));
      return;
    }
    if (e->kind == NK::Call) {
      emit_call(static_cast<Call*>(e));
      return;
    }
    rvalue(e);
  }

  void emit_local_decl(Decl* d) {
    if (d->is_const) return;  // constants and local procedures need no code
    std::vector<llvm::Value*> vals;
    if (d->values.size() == 1 && d->names.size() > 1) {
      llvm::Value* tuple = rvalue(d->values[0]);
      Type* tt = d->values[0]->type;
      for (size_t i = 0; i < d->names.size(); ++i)
        vals.push_back(convert(b_.CreateExtractValue(tuple, i), tt->elems[i], d->syms[i]->type));
    } else {
      for (Expr* v : d->values) vals.push_back(rvalue(v));
    }
    for (size_t i = 0; i < d->syms.size(); ++i) {
      Symbol* s = d->syms[i];
      auto* a = make_alloca(s->type, s->name);
      if ((uint64_t)s->align > a->getAlign().value()) a->setAlignment(llvm::Align((uint64_t)s->align));  // #align
      s->llvm_value = a;
      if (!vals.empty()) b_.CreateStore(vals[i], a);
      else if (!d->uninit) b_.CreateStore(default_value(s->type), a);
    }
  }

  void emit_assign(Assign* a) {
    if (a->overload_call) {
      emit_expr_any(a->overload_call);
      return;
    }
    if (a->op == Tok::Eq) {
      if (a->rhs.size() == 1 && a->lhs.size() > 1) {
        std::vector<llvm::Value*> addrs;
        for (Expr* l : a->lhs) addrs.push_back(lvalue(l));
        llvm::Value* tuple = rvalue(a->rhs[0]);
        Type* tt = a->rhs[0]->type;
        for (size_t i = 0; i < a->lhs.size(); ++i)
          b_.CreateStore(convert(b_.CreateExtractValue(tuple, i), tt->elems[i], a->lhs[i]->type), addrs[i]);
        return;
      }
      if (a->lhs.size() == 1) {
        llvm::Value* v = rvalue(a->rhs[0]);
        b_.CreateStore(v, lvalue(a->lhs[0]));
        return;
      }
      // Parallel assignment: evaluate everything first.
      std::vector<llvm::Value*> vals, addrs;
      for (Expr* r : a->rhs) vals.push_back(rvalue(r));
      for (Expr* l : a->lhs) addrs.push_back(lvalue(l));
      for (size_t i = 0; i < vals.size(); ++i) b_.CreateStore(vals[i], addrs[i]);
      return;
    }
    Expr* l = a->lhs[0];
    Expr* r = a->rhs[0];
    llvm::Value* addr = lvalue(l);
    llvm::Value* cur = b_.CreateLoad(lt(l->type), addr);
    llvm::Value* rv = rvalue(r);
    BinOp op;
    switch (a->op) {
      case Tok::PlusEq: op = BinOp::Add; break;
      case Tok::MinusEq: op = BinOp::Sub; break;
      case Tok::StarEq: op = BinOp::Mul; break;
      case Tok::SlashEq: op = BinOp::Div; break;
      case Tok::PercentEq: op = BinOp::Mod; break;
      case Tok::AmpEq: op = BinOp::BitAnd; break;
      case Tok::PipeEq: op = BinOp::BitOr; break;
      case Tok::CaretEq: op = BinOp::BitXor; break;
      case Tok::ShlEq: op = BinOp::Shl; break;
      default: op = BinOp::Shr; break;
    }
    llvm::Value* res = arith(op, cur, rv, l->type, r->type);
    b_.CreateStore(res, addr);
  }

  void emit_if(If* s) {
    if (s->cond->is_const() && s->cond->cv.kind == ConstValue::Bool) {
      // Constant condition: emit only the live branch.
      if (s->cond->cv.b) emit_body(s->then_s);
      else if (s->else_s) emit_body(s->else_s);
      return;
    }
    llvm::Value* c = cond(s->cond);
    auto* then_bb = llvm::BasicBlock::Create(ctx_, "if.then", fn_);
    auto* end_bb = llvm::BasicBlock::Create(ctx_, "if.end");
    llvm::BasicBlock* else_bb = s->else_s ? llvm::BasicBlock::Create(ctx_, "if.else") : end_bb;
    b_.CreateCondBr(c, then_bb, else_bb);
    b_.SetInsertPoint(then_bb);
    emit_body(s->then_s);
    if (!terminated()) b_.CreateBr(end_bb);
    if (s->else_s) {
      else_bb->insertInto(fn_);
      b_.SetInsertPoint(else_bb);
      emit_body(s->else_s);
      if (!terminated()) b_.CreateBr(end_bb);
    }
    end_bb->insertInto(fn_);
    b_.SetInsertPoint(end_bb);
  }

  void emit_switch(Switch* sw) {
    llvm::Value* v = rvalue(sw->subject);
    auto* end_bb = llvm::BasicBlock::Create(ctx_, "switch.end");
    std::vector<llvm::BasicBlock*> blocks;
    llvm::BasicBlock* default_bb = end_bb;
    for (auto& c : sw->cases) {
      auto* bb = llvm::BasicBlock::Create(ctx_, c.values.empty() ? "case.default" : "case", fn_);
      blocks.push_back(bb);
      if (c.values.empty()) default_bb = bb;
    }
    if (sw->subject->type->kind == TypeKind::String) {
      // Strings: compare against each case value in order.
      for (size_t i = 0; i < sw->cases.size(); ++i) {
        for (Expr* cv : sw->cases[i].values) {
          auto* next = llvm::BasicBlock::Create(ctx_, "case.next", fn_);
          b_.CreateCondBr(compare(BinOp::Eq, v, const_value(cv->cv, sw->subject->type), sw->subject->type),
                          blocks[i], next);
          b_.SetInsertPoint(next);
        }
      }
      b_.CreateBr(default_bb);
    } else {
      auto* si = b_.CreateSwitch(v, default_bb, (unsigned)sw->cases.size());
      for (size_t i = 0; i < sw->cases.size(); ++i) {
        for (Expr* cv : sw->cases[i].values)
          si->addCase(llvm::cast<llvm::ConstantInt>(const_value(cv->cv, sw->subject->type)), blocks[i]);
      }
    }
    for (size_t i = 0; i < sw->cases.size(); ++i) {
      b_.SetInsertPoint(blocks[i]);
      emit_scoped(sw->cases[i].body);
      if (!terminated()) {
        if (sw->cases[i].fallthrough && i + 1 < blocks.size()) b_.CreateBr(blocks[i + 1]);
        else b_.CreateBr(end_bb);
      }
    }
    end_bb->insertInto(fn_);
    b_.SetInsertPoint(end_bb);
  }

  void push_loop(Stmt* loop, llvm::BasicBlock* brk, llvm::BasicBlock* cont) {
    CgScope s;
    s.loop = loop;
    s.break_bb = brk;
    s.continue_bb = cont;
    scopes_.push_back(s);
  }

  void emit_while(While* w) {
    auto* cond_bb = llvm::BasicBlock::Create(ctx_, "while.cond", fn_);
    auto* body_bb = llvm::BasicBlock::Create(ctx_, "while.body");
    auto* end_bb = llvm::BasicBlock::Create(ctx_, "while.end");
    b_.CreateBr(cond_bb);
    b_.SetInsertPoint(cond_bb);
    b_.CreateCondBr(cond(w->cond), body_bb, end_bb);
    body_bb->insertInto(fn_);
    b_.SetInsertPoint(body_bb);
    push_loop(w, end_bb, cond_bb);
    emit_body(w->body);
    scopes_.pop_back();
    if (!terminated()) b_.CreateBr(cond_bb);
    end_bb->insertInto(fn_);
    b_.SetInsertPoint(end_bb);
  }

  // A for over an array whose body removes elements: the array's address
  // and the index variable, for `remove it`.
  struct RemoveLoop {
    llvm::Value* array;
    llvm::AllocaInst* index;
  };
  std::unordered_map<For*, RemoveLoop> remove_loops_;

  // `remove it;`: the last element moves into the current one's place, the
  // count shrinks, and (going forward) the index is visited again.
  void emit_remove(Remove* r) {
    if (r->lowered) {
      emit_stmt(r->lowered);
      return;
    }
    For* f = r->target;
    const RemoveLoop& rl = remove_loops_.at(f);
    Type* at = f->iterable->type;
    llvm::Type* arr_t = lt(at);
    llvm::Type* elem_t = lt(at->elem);
    llvm::Value* count_p = b_.CreateStructGEP(arr_t, rl.array, 0);
    llvm::Value* data = b_.CreateLoad(ptr_, b_.CreateStructGEP(arr_t, rl.array, 1));
    llvm::Value* i = b_.CreateLoad(i64_, rl.index);
    llvm::Value* last = b_.CreateSub(b_.CreateLoad(i64_, count_p), llvm::ConstantInt::get(i64_, 1));
    b_.CreateStore(b_.CreateLoad(elem_t, b_.CreateInBoundsGEP(elem_t, data, {last})),
                   b_.CreateInBoundsGEP(elem_t, data, {i}));
    b_.CreateStore(last, count_p);
    if (!f->reverse) b_.CreateStore(b_.CreateSub(i, llvm::ConstantInt::get(i64_, 1)), rl.index);
  }

  void emit_for(For* f) {
    if (f->expansion) {  // a for_expansion
      emit_stmt(f->expansion);
      return;
    }
    auto* it_a = make_alloca(f->it_sym->type, f->it_name);
    f->it_sym->llvm_value = it_a;
    llvm::AllocaInst* idx_a = make_alloca(T.s64, f->index_name);
    if (f->index_sym) f->index_sym->llvm_value = idx_a;

    auto* body_bb = llvm::BasicBlock::Create(ctx_, "for.body");
    auto* step_bb = llvm::BasicBlock::Create(ctx_, "for.step");
    auto* end_bb = llvm::BasicBlock::Create(ctx_, "for.end");

    if (f->range_lo) {
      Type* t = f->range_lo->type;
      llvm::Value* lo = rvalue(f->range_lo);
      llvm::Value* hi = rvalue(f->range_hi);
      bool sgn = t->is_signed;
      llvm::Value* start = f->reverse ? hi : lo;
      llvm::Value* last = f->reverse ? lo : hi;
      llvm::Value* nonempty = sgn ? b_.CreateICmpSLE(lo, hi) : b_.CreateICmpULE(lo, hi);
      auto* init_bb = llvm::BasicBlock::Create(ctx_, "for.init", fn_);
      b_.CreateCondBr(nonempty, init_bb, end_bb);
      b_.SetInsertPoint(init_bb);
      b_.CreateStore(start, it_a);
      b_.CreateStore(llvm::ConstantInt::get(i64_, 0), idx_a);
      b_.CreateBr(body_bb);

      body_bb->insertInto(fn_);
      b_.SetInsertPoint(body_bb);
      push_loop(f, end_bb, step_bb);
      emit_body(f->body);
      scopes_.pop_back();
      if (!terminated()) b_.CreateBr(step_bb);

      step_bb->insertInto(fn_);
      b_.SetInsertPoint(step_bb);
      llvm::Value* cur = b_.CreateLoad(lt(t), it_a);
      llvm::Value* done = f->reverse ? (sgn ? b_.CreateICmpSLE(cur, last) : b_.CreateICmpULE(cur, last))
                                     : (sgn ? b_.CreateICmpSGE(cur, last) : b_.CreateICmpUGE(cur, last));
      auto* incr_bb = llvm::BasicBlock::Create(ctx_, "for.incr", fn_);
      b_.CreateCondBr(done, end_bb, incr_bb);
      b_.SetInsertPoint(incr_bb);
      llvm::Value* one = llvm::ConstantInt::get(lt(t), 1);
      b_.CreateStore(f->reverse ? b_.CreateSub(cur, one) : b_.CreateAdd(cur, one), it_a);
      llvm::Value* idx = b_.CreateLoad(i64_, idx_a);
      b_.CreateStore(b_.CreateAdd(idx, llvm::ConstantInt::get(i64_, 1)), idx_a);
      b_.CreateBr(body_bb);
    } else {
      Type* at = f->iterable->type;
      llvm::Value* count;
      llvm::Value* data;
      llvm::Value* array = nullptr;  // with remove: count and data are read again each iteration
      if (f->has_remove) {
        array = addr_of(f->iterable);
        count = b_.CreateLoad(i64_, b_.CreateStructGEP(lt(at), array, 0));
        data = nullptr;
        remove_loops_[f] = {array, idx_a};
      } else if (at->is_fixed_array()) {
        llvm::Value* addr = addr_of(f->iterable, /*read_only=*/!f->by_pointer);
        count = llvm::ConstantInt::get(i64_, at->count);
        data = addr;
      } else {
        llvm::Value* v = rvalue(f->iterable);
        count = b_.CreateExtractValue(v, 0);
        data = b_.CreateExtractValue(v, 1);
      }
      llvm::Type* elem_t = lt(at->elem);
      auto* cond_bb = llvm::BasicBlock::Create(ctx_, "for.cond", fn_);
      llvm::Value* start = f->reverse ? b_.CreateSub(count, llvm::ConstantInt::get(i64_, 1))
                                      : llvm::ConstantInt::get(i64_, 0);
      b_.CreateStore(start, idx_a);
      b_.CreateBr(cond_bb);
      b_.SetInsertPoint(cond_bb);
      llvm::Value* i = b_.CreateLoad(i64_, idx_a);
      if (array) count = b_.CreateLoad(i64_, b_.CreateStructGEP(lt(at), array, 0));
      llvm::Value* ok = f->reverse ? b_.CreateICmpSGE(i, llvm::ConstantInt::get(i64_, 0)) : b_.CreateICmpSLT(i, count);
      b_.CreateCondBr(ok, body_bb, end_bb);

      body_bb->insertInto(fn_);
      b_.SetInsertPoint(body_bb);
      if (array) data = b_.CreateLoad(ptr_, b_.CreateStructGEP(lt(at), array, 1));
      llvm::Value* ep = b_.CreateInBoundsGEP(elem_t, data, {i});
      b_.CreateStore(f->by_pointer ? ep : b_.CreateLoad(elem_t, ep), it_a);
      push_loop(f, end_bb, step_bb);
      emit_body(f->body);
      scopes_.pop_back();
      if (!terminated()) b_.CreateBr(step_bb);

      step_bb->insertInto(fn_);
      b_.SetInsertPoint(step_bb);
      llvm::Value* cur = b_.CreateLoad(i64_, idx_a);
      llvm::Value* one = llvm::ConstantInt::get(i64_, 1);
      b_.CreateStore(f->reverse ? b_.CreateSub(cur, one) : b_.CreateAdd(cur, one), idx_a);
      b_.CreateBr(cond_bb);
    }
    end_bb->insertInto(fn_);
    b_.SetInsertPoint(end_bb);
  }

  // The value a return statement gives for results `rets` (a tuple if several).
  llvm::Value* return_value(Return* r, const std::vector<Type*>& rets) {
    if (rets.empty()) return nullptr;
    if (rets.size() == 1 || r->values.size() == 1) return rvalue(r->values[0]);
    llvm::Value* agg = llvm::UndefValue::get(lt(T.tuple(rets)));
    for (size_t i = 0; i < rets.size(); ++i) agg = b_.CreateInsertValue(agg, rvalue(r->values[i]), i);
    return agg;
  }

  void emit_return(Return* r) {
    llvm::Value* ret = return_value(r, proc_->type->returns);
    emit_defers_down_to(0);
    if (terminated()) return;
    if (proc_->type->c_call) emit_c_ret(ret);
    else if (ret) b_.CreateRet(ret);
    else b_.CreateRetVoid();
  }

  // ------------------------------------------------------------------ expressions

  llvm::Value* cond(Expr* e) {
    llvm::Value* v = rvalue(e);
    return to_bool(v, e->type);
  }

  llvm::Value* to_bool(llvm::Value* v, Type* t) {
    switch (t->kind) {
      case TypeKind::Bool: return v;
      case TypeKind::Int: case TypeKind::Enum:
        return b_.CreateICmpNE(v, llvm::ConstantInt::get(v->getType(), 0));
      case TypeKind::Float:
        return b_.CreateFCmpUNE(v, llvm::ConstantFP::get(v->getType(), 0.0));
      case TypeKind::Pointer: case TypeKind::Proc: case TypeKind::Null:
        return b_.CreateIsNotNull(v);
      default:
        throw CompileError(SourceLoc{}, "internal error: bad condition type");
    }
  }

  // Can this expression be addressed directly (without spilling to a temporary)?
  bool addressable(Expr* e) {
    if (e->is_const()) return false;
    switch (e->kind) {
      case NK::Ident: return static_cast<Ident*>(e)->sym->kind == SymKind::Var;
      case NK::Unary: return static_cast<Unary*>(e)->op == UnOp::Deref;
      case NK::Index: return true;
      case NK::Member: {
        auto* m = static_cast<Member*>(e);
        return m->mkind == MemberKind::Field || m->mkind == MemberKind::ArrayCount ||
               m->mkind == MemberKind::ArrayData || m->mkind == MemberKind::ArrayAllocated ||
               m->mkind == MemberKind::ArrayAllocator || m->mkind == MemberKind::StringCount ||
               m->mkind == MemberKind::StringData;
      }
      default: return false;
    }
  }

  // Address of any value: lvalues directly, other values via a temporary.
  // With `read_only` (the code only reads through the address), an aggregate
  // constant is not copied: its read-only global is used.
  llvm::Value* addr_of(Expr* e, bool read_only = false) {
    if (addressable(e)) return lvalue(e);
    if (read_only && e->is_const() && e->cv.kind == ConstValue::Aggregate) return const_global(e->cv, e->type);
    llvm::Value* v = rvalue(e);
    auto* tmp = make_alloca(e->type, "tmp");
    b_.CreateStore(v, tmp);
    return tmp;
  }

  llvm::Value* lvalue(Expr* e) {
    switch (e->kind) {
      case NK::Ident: {
        Symbol* s = static_cast<Ident*>(e)->sym;
        if (s->is_context) return context_addr();
        if (!s->llvm_value) throw CompileError(e->loc, cat("internal error: no storage for '", s->name, "'"));
        return s->llvm_value;
      }
      case NK::Unary: return rvalue(static_cast<Unary*>(e)->operand);
      case NK::Index: return index_addr(static_cast<Index*>(e));
      case NK::Member: {
        auto* m = static_cast<Member*>(e);
        llvm::Value* base = m->through_pointer ? rvalue(m->base) : addr_of(m->base, /*read_only=*/m->base->is_const());
        Type* bt = m->through_pointer ? m->base->type->elem : m->base->type;
        switch (m->mkind) {
          case MemberKind::Field:
            for (int i : m->field_path) {
              base = field_addr(bt, base, i);
              bt = bt->fields[i].type;
            }
            return base;
          case MemberKind::ArrayCount:
          case MemberKind::StringCount:
            return b_.CreateStructGEP(lt(bt), base, 0);
          case MemberKind::ArrayData:
          case MemberKind::StringData:
            return b_.CreateStructGEP(lt(bt), base, 1);
          case MemberKind::ArrayAllocated:
            return b_.CreateStructGEP(lt(bt), base, 2);
          case MemberKind::ArrayAllocator:
            return b_.CreateStructGEP(lt(bt), base, 3);
          default:
            break;
        }
        break;
      }
      default:
        break;
    }
    throw CompileError(e->loc, "internal error: expression is not addressable");
  }

  llvm::Value* to_i64(llvm::Value* v, Type* t) {
    if (v->getType() == i64_) return v;
    return t->is_signed ? b_.CreateSExt(v, i64_) : b_.CreateZExt(v, i64_);
  }

  void bounds_check(llvm::Value* idx, llvm::Value* count, SourceLoc loc) {
    if (!opts_.bounds_checks || (proc_ && proc_->no_abc)) return;
    if (auto* ci = llvm::dyn_cast<llvm::ConstantInt>(idx)) {
      if (auto* cc = llvm::dyn_cast<llvm::ConstantInt>(count))
        if (ci->getSExtValue() >= 0 && ci->getSExtValue() < cc->getSExtValue()) return;
    }
    llvm::Value* bad = b_.CreateICmpUGE(idx, count);
    auto* fail_bb = llvm::BasicBlock::Create(ctx_, "bounds.fail", fn_);
    auto* ok_bb = llvm::BasicBlock::Create(ctx_, "bounds.ok", fn_);
    b_.CreateCondBr(bad, fail_bb, ok_bb, llvm::MDBuilder(ctx_).createUnlikelyBranchWeights());
    b_.SetInsertPoint(fail_bb);
    const std::string& path = sm_.get(loc.file).path;
    b_.CreateCall(rt_bounds_, {idx, count, cstring_global(path), llvm::ConstantInt::get(i64_, path.size()),
                               llvm::ConstantInt::get(i32_, loc.line)});
    b_.CreateUnreachable();
    b_.SetInsertPoint(ok_bb);
  }

  llvm::Value* index_addr(Index* ix) {
    Type* bt = ix->base->type;
    switch (bt->kind) {
      case TypeKind::Array: {
        if (bt->is_fixed_array()) {
          llvm::Value* base = addr_of(ix->base, /*read_only=*/ix->base->is_const());
          llvm::Value* i = to_i64(rvalue(ix->index), ix->index->type);
          bounds_check(i, llvm::ConstantInt::get(i64_, bt->count), ix->loc);
          return b_.CreateInBoundsGEP(lt(bt->elem), base, {i});
        }
        llvm::Value* v = rvalue(ix->base);
        llvm::Value* i = to_i64(rvalue(ix->index), ix->index->type);
        bounds_check(i, b_.CreateExtractValue(v, 0), ix->loc);
        return b_.CreateInBoundsGEP(lt(bt->elem), b_.CreateExtractValue(v, 1), {i});
      }
      case TypeKind::String: {
        llvm::Value* v = rvalue(ix->base);
        llvm::Value* i = to_i64(rvalue(ix->index), ix->index->type);
        bounds_check(i, b_.CreateExtractValue(v, 0), ix->loc);
        return b_.CreateInBoundsGEP(i8_, b_.CreateExtractValue(v, 1), {i});
      }
      case TypeKind::Pointer: {
        llvm::Value* p = rvalue(ix->base);
        llvm::Value* i = to_i64(rvalue(ix->index), ix->index->type);
        return b_.CreateGEP(lt(bt->elem), p, {i});
      }
      default:
        throw CompileError(ix->loc, "internal error: bad index base");
    }
  }

  llvm::Value* rvalue(Expr* e) {
    if (e->is_const()) return const_rvalue(e->cv, e->type);
    switch (e->kind) {
      case NK::Ident: {
        Symbol* s = static_cast<Ident*>(e)->sym;
        if (s->kind == SymKind::Proc) return s->proc->llvm_fn;
        return b_.CreateLoad(lt(s->type), lvalue(e), s->name);
      }
      case NK::Unary: return emit_unary(static_cast<Unary*>(e));
      case NK::Binary: return emit_binary(static_cast<Binary*>(e));
      case NK::Call: {
        llvm::Value* v = emit_call(static_cast<Call*>(e));
        if (!v) throw CompileError(e->loc, "internal error: void call used as value");
        return v;
      }
      case NK::Index: return b_.CreateLoad(lt(e->type), index_addr(static_cast<Index*>(e)));
      case NK::Member: return emit_member(static_cast<Member*>(e));
      case NK::Cast: {
        auto* c = static_cast<Cast*>(e);
        return emit_conversion(c->operand, c->type);
      }
      case NK::ImplicitConv: {
        auto* ic = static_cast<ImplicitConv*>(e);
        return emit_conversion(ic->operand, ic->type);
      }
      case NK::TupleGet: {
        auto* g = static_cast<TupleGet*>(e);
        return b_.CreateExtractValue(rvalue(g->operand), g->index);
      }
      case NK::StructLit: {
        auto* s = static_cast<StructLit*>(e);
        Type* t = s->type;
        if (t->is_union) {
          auto* tmp = make_alloca(t, "union.tmp");
          b_.CreateStore(llvm::Constant::getNullValue(lt(t)), tmp);
          for (size_t i = 0; i < s->field_values.size(); ++i)
            if (s->field_values[i]) b_.CreateStore(rvalue(s->field_values[i]), tmp);
          return b_.CreateLoad(lt(t), tmp);
        }
        llvm::Constant* defaults = default_value(t);
        if (in_memory(t) || defaults->getType() != lt(t)) {
          // Built in memory: the defaults (but not of members overlapping a
          // given one, #place), then the given members.
          auto* tmp = make_alloca(t, "struct.tmp");
          b_.CreateStore(struct_const(t, field_defaults(t, s->field_values)), tmp);
          for (size_t i = 0; i < s->field_values.size(); ++i)
            if (s->field_values[i]) b_.CreateStore(rvalue(s->field_values[i]), field_addr(t, tmp, (int)i));
          return b_.CreateLoad(lt(t), tmp);
        }
        llvm::Value* agg = defaults;
        for (size_t i = 0; i < s->field_values.size(); ++i)
          if (s->field_values[i]) agg = b_.CreateInsertValue(agg, rvalue(s->field_values[i]), i);
        return agg;
      }
      case NK::ArrayLit: {
        auto* a = static_cast<ArrayLit*>(e);
        llvm::Value* agg = llvm::Constant::getNullValue(lt(a->type));
        for (size_t i = 0; i < a->elems.size(); ++i) agg = b_.CreateInsertValue(agg, rvalue(a->elems[i]), i);
        return agg;
      }
      case NK::ProcLit: return static_cast<ProcLit*>(e)->llvm_fn;
      case NK::Expansion: {
        llvm::Value* v = emit_expansion(static_cast<MacroExpansion*>(e));
        if (!v) throw CompileError(e->loc, "internal error: void macro expansion used as value");
        return v;
      }
      case NK::Ifx: {
        auto* x = static_cast<Ifx*>(e);
        llvm::Value* c = cond(x->cond);
        auto* then_bb = llvm::BasicBlock::Create(ctx_, "ifx.then", fn_);
        auto* else_bb = llvm::BasicBlock::Create(ctx_, "ifx.else", fn_);
        auto* end_bb = llvm::BasicBlock::Create(ctx_, "ifx.end", fn_);
        b_.CreateCondBr(c, then_bb, else_bb);
        b_.SetInsertPoint(then_bb);
        llvm::Value* a = rvalue(x->then_e);
        llvm::BasicBlock* a_end = b_.GetInsertBlock();
        b_.CreateBr(end_bb);
        b_.SetInsertPoint(else_bb);
        llvm::Value* bv = rvalue(x->else_e);
        llvm::BasicBlock* b_end = b_.GetInsertBlock();
        b_.CreateBr(end_bb);
        b_.SetInsertPoint(end_bb);
        auto* phi = b_.CreatePHI(lt(e->type), 2);
        phi->addIncoming(a, a_end);
        phi->addIncoming(bv, b_end);
        return phi;
      }
      default:
        break;
    }
    throw CompileError(e->loc, "internal error: cannot generate code for expression");
  }

  llvm::Value* emit_member(Member* m) {
    switch (m->mkind) {
      case MemberKind::Field:
        if (m->through_pointer || addressable(m->base)) return b_.CreateLoad(lt(m->type), lvalue(m));
        {
          std::vector<unsigned> idx;
          for (Type* t = m->base->type; int i : m->field_path) {
            if (in_memory(t)) return b_.CreateLoad(lt(m->type), lvalue(m));
            idx.push_back(i);
            t = t->fields[i].type;
          }
          return b_.CreateExtractValue(rvalue(m->base), idx);
        }
      case MemberKind::ArrayData:
        if (m->base->type->is_fixed_array() ||
            (m->through_pointer && m->base->type->elem->is_fixed_array())) {
          return m->through_pointer ? rvalue(m->base) : addr_of(m->base);
        }
        [[fallthrough]];
      case MemberKind::ArrayAllocator:
        return b_.CreateLoad(lt(m->type), lvalue(m));
      case MemberKind::ArrayCount:
      case MemberKind::ArrayAllocated:
      case MemberKind::StringCount:
      case MemberKind::StringData: {
        if (m->through_pointer) return b_.CreateLoad(lt(m->type), lvalue(m));
        unsigned idx = (m->mkind == MemberKind::ArrayCount || m->mkind == MemberKind::StringCount) ? 0
                       : (m->mkind == MemberKind::ArrayAllocated)                                    ? 2
                                                                                                     : 1;
        return b_.CreateExtractValue(rvalue(m->base), idx);
      }
      case MemberKind::AnyType:
        return b_.CreateExtractValue(rvalue(m->base), 0);
      case MemberKind::AnyValue:
        return b_.CreateExtractValue(rvalue(m->base), 1);
      default:
        throw CompileError(m->loc, "internal error: unresolved member");
    }
  }

  llvm::Value* emit_unary(Unary* u) {
    switch (u->op) {
      case UnOp::AddrOf:
        if (u->operand->type->is_proc()) return rvalue(u->operand);
        return lvalue(u->operand);
      case UnOp::Deref:
        return b_.CreateLoad(lt(u->type), rvalue(u->operand));
      case UnOp::Not:
        return b_.CreateNot(cond(u->operand));
      case UnOp::Plus:
        return rvalue(u->operand);
      case UnOp::Neg: {
        llvm::Value* v = rvalue(u->operand);
        if (u->type->kind == TypeKind::Float) return b_.CreateFNeg(v);
        return b_.CreateNeg(v);
      }
      case UnOp::BitNot:
        return b_.CreateNot(rvalue(u->operand));
    }
    return nullptr;
  }

  // Integer-ish signedness for enums and ints.
  static bool is_signed_int(Type* t) { return (t->kind == TypeKind::Int || t->kind == TypeKind::Enum) && t->is_signed; }

  llvm::Value* arith(BinOp op, llvm::Value* a, llvm::Value* b, Type* t, Type* bt) {
    if (t->is_pointer()) {
      llvm::Value* off = to_i64(b, bt);
      if (op == BinOp::Sub) off = b_.CreateNeg(off);
      return b_.CreateGEP(mem_t(t->elem), a, {off});
    }
    if (op == BinOp::Shl || op == BinOp::Shr) {
      if (b->getType() != a->getType()) b = b_.CreateZExtOrTrunc(b, a->getType());
      if (op == BinOp::Shl) return b_.CreateShl(a, b);
      return is_signed_int(t) ? b_.CreateAShr(a, b) : b_.CreateLShr(a, b);
    }
    if (t->kind == TypeKind::Float) {
      switch (op) {
        case BinOp::Add: return b_.CreateFAdd(a, b);
        case BinOp::Sub: return b_.CreateFSub(a, b);
        case BinOp::Mul: return b_.CreateFMul(a, b);
        case BinOp::Div: return b_.CreateFDiv(a, b);
        default: break;
      }
      throw CompileError(SourceLoc{}, "internal error: bad float op");
    }
    bool sgn = is_signed_int(t);
    switch (op) {
      case BinOp::Add: return b_.CreateAdd(a, b);
      case BinOp::Sub: return b_.CreateSub(a, b);
      case BinOp::Mul: return b_.CreateMul(a, b);
      case BinOp::Div: return sgn ? b_.CreateSDiv(a, b) : b_.CreateUDiv(a, b);
      case BinOp::Mod: return sgn ? b_.CreateSRem(a, b) : b_.CreateURem(a, b);
      case BinOp::BitAnd: return b_.CreateAnd(a, b);
      case BinOp::BitOr: return b_.CreateOr(a, b);
      case BinOp::BitXor: return b_.CreateXor(a, b);
      default: break;
    }
    throw CompileError(SourceLoc{}, "internal error: bad integer op");
  }

  llvm::Value* compare(BinOp op, llvm::Value* a, llvm::Value* b, Type* t) {
    if (t->kind == TypeKind::String) {
      llvm::Value* eq = b_.CreateCall(rt_streq_, {b_.CreateExtractValue(a, 1), b_.CreateExtractValue(a, 0),
                                                  b_.CreateExtractValue(b, 1), b_.CreateExtractValue(b, 0)});
      llvm::Value* r = b_.CreateICmpNE(eq, llvm::ConstantInt::get(i32_, 0));
      return op == BinOp::Eq ? r : b_.CreateNot(r);
    }
    if (t->kind == TypeKind::Float) {
      switch (op) {
        case BinOp::Eq: return b_.CreateFCmpOEQ(a, b);
        case BinOp::Ne: return b_.CreateFCmpUNE(a, b);
        case BinOp::Lt: return b_.CreateFCmpOLT(a, b);
        case BinOp::Le: return b_.CreateFCmpOLE(a, b);
        case BinOp::Gt: return b_.CreateFCmpOGT(a, b);
        default: return b_.CreateFCmpOGE(a, b);
      }
    }
    bool sgn = is_signed_int(t);
    switch (op) {
      case BinOp::Eq: return b_.CreateICmpEQ(a, b);
      case BinOp::Ne: return b_.CreateICmpNE(a, b);
      case BinOp::Lt: return sgn ? b_.CreateICmpSLT(a, b) : b_.CreateICmpULT(a, b);
      case BinOp::Le: return sgn ? b_.CreateICmpSLE(a, b) : b_.CreateICmpULE(a, b);
      case BinOp::Gt: return sgn ? b_.CreateICmpSGT(a, b) : b_.CreateICmpUGT(a, b);
      default: return sgn ? b_.CreateICmpSGE(a, b) : b_.CreateICmpUGE(a, b);
    }
  }

  llvm::Value* emit_binary(Binary* bin) {
    if (bin->op == BinOp::LogAnd || bin->op == BinOp::LogOr) {
      bool is_and = bin->op == BinOp::LogAnd;
      llvm::Value* l = cond(bin->lhs);
      llvm::BasicBlock* lhs_end = b_.GetInsertBlock();
      auto* rhs_bb = llvm::BasicBlock::Create(ctx_, is_and ? "and.rhs" : "or.rhs", fn_);
      auto* end_bb = llvm::BasicBlock::Create(ctx_, is_and ? "and.end" : "or.end", fn_);
      if (is_and) b_.CreateCondBr(l, rhs_bb, end_bb);
      else b_.CreateCondBr(l, end_bb, rhs_bb);
      b_.SetInsertPoint(rhs_bb);
      llvm::Value* r = cond(bin->rhs);
      llvm::BasicBlock* rhs_end = b_.GetInsertBlock();
      b_.CreateBr(end_bb);
      b_.SetInsertPoint(end_bb);
      auto* phi = b_.CreatePHI(i1_, 2);
      phi->addIncoming(llvm::ConstantInt::get(i1_, is_and ? 0 : 1), lhs_end);
      phi->addIncoming(r, rhs_end);
      return phi;
    }
    Type* lt_ = bin->lhs->type;
    Type* rt_ = bin->rhs->type;
    llvm::Value* a = rvalue(bin->lhs);
    llvm::Value* b = rvalue(bin->rhs);
    switch (bin->op) {
      case BinOp::Eq: case BinOp::Ne: case BinOp::Lt: case BinOp::Le: case BinOp::Gt: case BinOp::Ge:
        return compare(bin->op, a, b, lt_);
      default:
        break;
    }
    if (bin->op == BinOp::Sub && lt_->is_pointer() && rt_->is_pointer())
      return b_.CreateSExtOrTrunc(b_.CreatePtrDiff(mem_t(lt_->elem), a, b), i64_);
    return arith(bin->op, a, b, lt_, rt_);
  }

  // Converts the value of `e` to type `to` (casts and implicit conversions).
  llvm::Value* emit_conversion(Expr* e, Type* to) {
    Type* from = e->type;
    if (to->kind == TypeKind::Any) {
      if (from->kind == TypeKind::Any) return rvalue(e);
      llvm::Value* addr = addr_of(e, /*read_only=*/true);  // Any values are read (print)
      llvm::Value* any = llvm::UndefValue::get(lt(T.any_t));
      any = b_.CreateInsertValue(any, type_info(from), 0);
      return b_.CreateInsertValue(any, addr, 1);
    }
    if (from->kind == TypeKind::Array && to->is_view()) {
      if (from->is_fixed_array()) {
        llvm::Value* addr = addr_of(e);
        llvm::Value* v = llvm::UndefValue::get(lt(to));
        v = b_.CreateInsertValue(v, llvm::ConstantInt::get(i64_, from->count), 0);
        return b_.CreateInsertValue(v, addr, 1);
      }
      llvm::Value* src = rvalue(e);
      llvm::Value* v = llvm::UndefValue::get(lt(to));
      v = b_.CreateInsertValue(v, b_.CreateExtractValue(src, 0), 0);
      return b_.CreateInsertValue(v, b_.CreateExtractValue(src, 1), 1);
    }
    return convert(rvalue(e), from, to);
  }

  llvm::Value* convert(llvm::Value* v, Type* from, Type* to) {
    if (from == to || from->underlying() == to->underlying()) return v;  // distinct types: the same representation
    auto intlike = [](Type* t) { return t->kind == TypeKind::Int || t->kind == TypeKind::Enum; };
    llvm::Type* dst = lt(to);
    if (intlike(from) && intlike(to)) {
      if (from->bits == to->bits) return v;
      if (from->bits > to->bits) return b_.CreateTrunc(v, dst);
      return from->is_signed ? b_.CreateSExt(v, dst) : b_.CreateZExt(v, dst);
    }
    if (intlike(from) && to->kind == TypeKind::Float)
      return from->is_signed ? b_.CreateSIToFP(v, dst) : b_.CreateUIToFP(v, dst);
    if (from->kind == TypeKind::Float && intlike(to))
      return to->is_signed ? b_.CreateFPToSI(v, dst) : b_.CreateFPToUI(v, dst);
    if (from->kind == TypeKind::Float && to->kind == TypeKind::Float)
      return from->bits < to->bits ? b_.CreateFPExt(v, dst) : b_.CreateFPTrunc(v, dst);
    if (from->kind == TypeKind::Bool && intlike(to)) return b_.CreateZExt(v, dst);
    if (intlike(from) && to->kind == TypeKind::Bool) return b_.CreateICmpNE(v, llvm::ConstantInt::get(v->getType(), 0));
    bool fp = from->is_pointer() || from->is_proc() || from->kind == TypeKind::Null || from->kind == TypeKind::TypeT;
    bool tp = to->is_pointer() || to->is_proc() || to->kind == TypeKind::TypeT;
    if (fp && tp) return v;
    if (fp && intlike(to)) return b_.CreatePtrToInt(v, dst);
    if (intlike(from) && tp) return b_.CreateIntToPtr(v, dst);
    throw CompileError(SourceLoc{}, cat("internal error: unsupported conversion from ", type_name(from), " to ",
                                        type_name(to)));
  }

  // ------------------------------------------------------------------ calls

  llvm::Value* emit_call(Call* c) {
    if (c->builtin != Builtin::None) return emit_builtin(c);
    if (c->is_const()) return const_rvalue(c->cv, c->type);
    llvm::Value* callee;
    Ident* id = c->callee->kind == NK::Ident ? static_cast<Ident*>(c->callee) : nullptr;
    if (id && id->sym->kind == SymKind::Proc) callee = id->sym->proc->llvm_fn;
    else callee = rvalue(c->callee);
    if (c->proc_type->c_call) return emit_c_call(c, callee);
    std::vector<llvm::Value*> args;
    for (Expr* a : c->resolved_args) args.push_back(rvalue(a));
    auto* call = b_.CreateCall(fn_type(c->proc_type), callee, args);
    if (c->force_inline) call->addFnAttr(llvm::Attribute::AlwaysInline);
    if (c->proc_type->returns.empty()) return nullptr;
    return call;
  }

  llvm::Value* string_parts(Expr* e, llvm::Value** len) {
    llvm::Value* s = rvalue(e);
    *len = b_.CreateExtractValue(s, 0);
    return b_.CreateExtractValue(s, 1);
  }

  // Builds a stack array of Any from already-converted Any expressions.
  llvm::Value* any_array(const std::vector<Expr*>& args, size_t first, size_t end) {
    size_t n = end - first;
    if (n == 0) return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));
    auto* arr_t = llvm::ArrayType::get(lt(T.any_t), n);
    auto* arr = make_alloca_raw(arr_t, "print.args");
    for (size_t i = first; i < end; ++i) {
      llvm::Value* slot = b_.CreateConstInBoundsGEP2_32(arr_t, arr, 0, (unsigned)(i - first));
      b_.CreateStore(rvalue(args[i]), slot);
    }
    return arr;
  }

  // The Any values of a print / sprint call: its arguments, or the `..array` given.
  std::pair<llvm::Value*, llvm::Value*> print_args(Call* c) {
    auto& a = c->resolved_args;
    if (c->spread_args) {
      llvm::Value* view = rvalue(a[1]);
      return {b_.CreateExtractValue(view, 1), b_.CreateExtractValue(view, 0)};
    }
    size_t n_args = a.size() - 1;  // the last entry is to_standard_error
    return {any_array(a, 1, n_args), llvm::ConstantInt::get(i64_, n_args - 1)};
  }

  llvm::Value* emit_math1(llvm::Intrinsic::ID id, llvm::Value* x) { return b_.CreateUnaryIntrinsic(id, x); }

  llvm::Value* libm_call(const char* name64, const char* name32, Type* t, std::vector<llvm::Value*> args) {
    llvm::Type* ft = lt(t);
    std::vector<llvm::Type*> ps(args.size(), ft);
    auto callee = mod_->getOrInsertFunction(t->bits == 32 ? name32 : name64, llvm::FunctionType::get(ft, ps, false));
    return b_.CreateCall(callee, args);
  }

  // ---- allocation
  //
  // Programs that use the context allocate through __allocate (preload.jai),
  // which calls the allocator given (or context.allocator); others call the
  // runtime's heap directly, as before.

  llvm::Value* null_ptr() { return llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0)); }

  // The explicit allocator of an allocating built-in (a pointer to an
  // Allocator), or null for context.allocator.
  llvm::Value* allocator_arg(Call* c) { return c->allocator ? addr_of(c->allocator, /*read_only=*/true) : null_ptr(); }

  llvm::Value* allocate(AllocatorMode mode, llvm::Value* size, llvm::Value* old_size, llvm::Value* old_memory,
                        llvm::Value* allocator) {
    llvm::Function* f = prog_.allocate_proc->llvm_fn;
    return b_.CreateCall(f, {llvm::ConstantInt::get(f->getArg(0)->getType(), mode), size, old_size, old_memory,
                             allocator});
  }

  // `t`: the type allocated (New / NewArray), whose alignment may be more than the heap's.
  llvm::Value* alloc_bytes(Call* c, llvm::Value* size, Type* t = nullptr) {
    if (t && t->align > T.heap_align()) {
      llvm::Value* al = llvm::ConstantInt::get(i64_, (uint64_t)t->align);
      if (!prog_.uses_context) return b_.CreateCall(rt_alloc_aligned_, {size, al});
      return b_.CreateCall(prog_.allocate_aligned_proc->llvm_fn, {size, al, allocator_arg(c)});
    }
    if (!prog_.uses_context) return b_.CreateCall(rt_alloc_, {size});
    return allocate(MODE_ALLOCATE, size, llvm::ConstantInt::get(i64_, 0), null_ptr(), allocator_arg(c));
  }

  // __ojay_array_reserve's alignment arguments for elements of type t: 0 and
  // null unless t is aligned more than the heap guarantees (then the array
  // moves to aligned blocks, through __allocate_aligned with a context).
  std::pair<llvm::Value*, llvm::Value*> reserve_alignment(Type* t) {
    if (t->align <= T.heap_align()) return {llvm::ConstantInt::get(i64_, 0), null_ptr()};
    llvm::Value* fn = prog_.uses_context ? (llvm::Value*)prog_.allocate_aligned_proc->llvm_fn : null_ptr();
    return {llvm::ConstantInt::get(i64_, (uint64_t)t->align), fn};
  }

  // Frees `p` with the allocator at `allocator` (null: the context's).
  void free_bytes(llvm::Value* p, llvm::Value* allocator) {
    if (!prog_.uses_context) {
      b_.CreateCall(rt_free_, {p});
      return;
    }
    llvm::Value* zero = llvm::ConstantInt::get(i64_, 0);
    allocate(MODE_FREE, zero, zero, p, allocator);
  }

  // The (allocate, allocator) arguments of the runtime functions that allocate:
  // __allocate and the Allocator to use, or nulls for the runtime's heap.
  std::pair<llvm::Value*, llvm::Value*> runtime_allocator(llvm::Value* allocator) {
    if (!prog_.uses_context) return {null_ptr(), null_ptr()};
    return {prog_.allocate_proc->llvm_fn, allocator};
  }

  void store_default(llvm::Value* ptr, Type* t) {
    llvm::Constant* d = default_value(t);
    if (d->isNullValue()) {
      b_.CreateMemSet(ptr, llvm::ConstantInt::get(i8_, 0), size_of(t), llvm::MaybeAlign(1));
    } else {
      b_.CreateStore(d, ptr);
    }
  }

  // The memory dynamic array `arr` (of LLVM type dyn_t) owns: its data, or
  // null when allocated is 0 (a baked #run result points to a global).
  llvm::Value* owned_data(llvm::Type* dyn_t, llvm::Value* arr) {
    llvm::Value* data = b_.CreateLoad(ptr_, b_.CreateStructGEP(dyn_t, arr, 1));
    llvm::Value* allocated = b_.CreateLoad(i64_, b_.CreateStructGEP(dyn_t, arr, 2));
    return b_.CreateSelect(b_.CreateICmpSGT(allocated, llvm::ConstantInt::get(i64_, 0)), data, null_ptr());
  }

  // `#asm { ... }`: one volatile inline asm call (Intel syntax on x86-64).
  // Each variable operand is read and written: its value goes in as a tied
  // input of an output register, and the output is stored back.
  // The register family of `name` on this target (the name LLVM's constraints
  // use for it, whatever the width: x86 "ax" for rax / eax / ax / al), or ""
  // when it is not a register that #asm may pin or clobber.
  static std::string asm_register(const std::string& name, bool x86, bool arm, bool riscv) {
    std::string n;
    for (char ch : name) n += (char)std::tolower((unsigned char)ch);
    if (x86) {
      static const std::unordered_map<std::string, std::string> legacy = {
          {"a", "ax"}, {"b", "bx"}, {"c", "cx"}, {"d", "dx"},  // Jai's short names
          {"rax", "ax"}, {"eax", "ax"}, {"ax", "ax"}, {"al", "ax"}, {"ah", "ax"},
          {"rbx", "bx"}, {"ebx", "bx"}, {"bx", "bx"}, {"bl", "bx"}, {"bh", "bx"},
          {"rcx", "cx"}, {"ecx", "cx"}, {"cx", "cx"}, {"cl", "cx"}, {"ch", "cx"},
          {"rdx", "dx"}, {"edx", "dx"}, {"dx", "dx"}, {"dl", "dx"}, {"dh", "dx"},
          {"rsi", "si"}, {"esi", "si"}, {"si", "si"}, {"sil", "si"},
          {"rdi", "di"}, {"edi", "di"}, {"di", "di"}, {"dil", "di"}};
      if (auto it = legacy.find(n); it != legacy.end()) return it->second;
      // r8 .. r15 (and their d / w / b forms), xmm0 .. xmm15
      size_t k = 0;
      if (n.size() >= 2 && n[0] == 'r' && std::isdigit((unsigned char)n[1])) {
        k = 1;
        while (k < n.size() && std::isdigit((unsigned char)n[k])) ++k;
        int r = std::stoi(n.substr(1, k - 1));
        std::string suffix = n.substr(k);
        if (r >= 8 && r <= 15 && (suffix.empty() || suffix == "d" || suffix == "w" || suffix == "b"))
          return "r" + std::to_string(r);
        return "";
      }
      for (const char* v : {"xmm", "ymm"})
        if (n.rfind(v, 0) == 0 && n.size() > 3 && n.find_first_not_of("0123456789", 3) == std::string::npos) {
          int r = std::stoi(n.substr(3));
          if (r <= 15) return "xmm" + std::to_string(r);
        }
      return "";  // (not rsp / rbp / rip: the stack and frame pointers)
    }
    if (arm) {
      if (n == "lr") return "lr";
      if (n == "ip") return "r12";
      auto num = [&](char p, int max) -> std::string {
        if (n.size() < 2 || n[0] != p || n.find_first_not_of("0123456789", 1) != std::string::npos) return "";
        int r = std::stoi(n.substr(1));
        return r <= max ? std::string(1, p) + std::to_string(r) : "";
      };
      std::string r = num('r', 12);
      if (!r.empty()) return r == "r7" ? "" : r;  // (r7: Thumb's frame pointer)
      if (!(r = num('s', 31)).empty()) return r;
      if (!(r = num('d', 15)).empty()) return r;
      return "";  // (not sp / pc)
    }
    if (riscv) {
      static const std::unordered_set<std::string> abi = {
          "ra", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
          "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
          "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7", "ft8", "ft9", "ft10", "ft11",
          "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7",
          "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7", "fs8", "fs9", "fs10", "fs11"};
      if (abi.count(n)) return n;
      if (n.size() >= 2 && n[0] == 'x' && n.find_first_not_of("0123456789", 1) == std::string::npos) {
        int r = std::stoi(n.substr(1));
        static const char* names[32] = {"", "ra", "", "", "", "t0", "t1", "t2", "", "s1", "a0", "a1", "a2",
                                        "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
                                        "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
        return r < 32 ? names[r] : "";  // (not zero / sp / gp / tp / s0 = fp)
      }
      return "";
    }
    return "";
  }

  void emit_asm(AsmStmt* a) {
    llvm::Triple tr(mod_->getTargetTriple());
    bool x86 = tr.isX86(), arm = tr.isARM() || tr.isThumb(), riscv = tr.isRISCV();
    std::vector<llvm::Type*> types;     // the outputs' types
    std::vector<llvm::Value*> inputs;
    std::vector<llvm::Type*> in_types;
    std::string outs, ins;
    std::set<std::string> pinned;
    size_t n = a->operands.size();
    for (size_t i = 0; i < n; ++i) {
      const AsmStmt::Operand& op = a->operands[i];
      llvm::Type* ty;
      bool is_float;
      if (op.var) {
        Type* t = const_cast<Type*>(op.var->type->underlying());
        ty = lt(op.var->type);
        is_float = t->kind == TypeKind::Float;
      } else if (op.vec) {
        ty = x86 ? (llvm::Type*)llvm::Type::getDoubleTy(ctx_) : (llvm::Type*)llvm::Type::getFloatTy(ctx_);
        is_float = true;
      } else {
        ty = tr.isArch64Bit() ? i64_ : i32_;
        is_float = false;
      }
      std::string c = "r";
      if (is_float) {
        if (x86) c = "x";
        else if (arm) c = ty->isFloatTy() ? "t" : "w";
        else
          throw CompileError(a->loc, cat("#asm: '", op.name, "': this target has no float registers"));
      }
      if (!op.pin.empty()) {
        std::string reg = asm_register(op.pin, x86, arm, riscv);
        if (reg.empty())
          throw CompileError(a->loc, cat("#asm: '", op.pin, "' (for '", op.name, "') is not a register that can be "
                                         "pinned on this target"));
        if (!pinned.insert(reg).second)
          throw CompileError(a->loc, cat("#asm: two operands are pinned to register '", op.pin, "'"));
        c = "{" + reg + "}";
      }
      types.push_back(ty);
      // A variable is read before the block (tied to its output); a declared
      // register has no value then (an early-clobber output: the block may
      // write it before it reads the inputs).
      outs += (i ? "," : "") + std::string(op.var ? "=" : "=&") + c;
      if (op.var) {
        inputs.push_back(b_.CreateLoad(ty, op.var->llvm_value));
        in_types.push_back(ty);
        ins += "," + std::to_string(i);
      }
    }
    std::string constraints = outs + ins;
    std::string clobbers = "~{memory}";
    if (x86) clobbers += ",~{dirflag},~{fpsr},~{flags}";
    else if (arm) clobbers += ",~{cc}";
    // The registers named in the block are clobbered (unless they are operands).
    for (const std::string& w : a->literal_words) {
      std::string reg = asm_register(w, x86, arm, riscv);
      if (!reg.empty() && !pinned.count(reg)) {
        clobbers += ",~{" + reg + "}";
        pinned.insert(reg);  // (once)
      }
    }
    constraints += (constraints.empty() ? "" : ",") + clobbers;
    llvm::Type* rt = n == 0 ? void_ : n == 1 ? types[0] : llvm::StructType::get(ctx_, types);
    auto* ft = llvm::FunctionType::get(rt, in_types, false);
    if (llvm::Error err = llvm::InlineAsm::verify(ft, constraints))
      throw CompileError(a->loc, cat("internal error: #asm constraints: ", llvm::toString(std::move(err))));
    auto* ia = llvm::InlineAsm::get(ft, a->asm_text, constraints, /*hasSideEffects=*/true, /*isAlignStack=*/false,
                                    x86 ? llvm::InlineAsm::AD_Intel : llvm::InlineAsm::AD_ATT);
    auto* call = b_.CreateCall(ft, ia, inputs);
    // Where the block is, for the assembler's errors: (file + 1) << 32 | line << 12 | column (see the driver).
    uint64_t cookie = ((uint64_t)a->text_loc.file + 1) << 32 | (uint64_t)(a->text_loc.line & 0xFFFFF) << 12 |
                      std::min<uint32_t>(a->text_loc.col, 4095);
    call->setMetadata("srcloc", llvm::MDNode::get(ctx_, {llvm::ConstantAsMetadata::get(
                                                           llvm::ConstantInt::get(i64_, cookie))}));
    llvm::Value* r = call;
    for (size_t i = 0; i < n; ++i) {
      if (!a->operands[i].var) continue;  // a declared register: its value is dropped
      llvm::Value* v = n == 1 ? r : b_.CreateExtractValue(r, (unsigned)i);
      b_.CreateStore(v, a->operands[i].var->llvm_value);
    }
  }

  // Type_Info_Struct.initializer: `(p: *void) { p.* = <t's default>; }`.
  llvm::Function* initializer_fn(Type* t, llvm::Constant* def, const std::string& name) {
    auto* ft = llvm::FunctionType::get(void_, {ptr_}, false);
    auto* f = llvm::Function::Create(ft, llvm::GlobalValue::PrivateLinkage, "__initializer." + name, mod_.get());
    f->addFnAttr(llvm::Attribute::NoUnwind);
    llvm::IRBuilder<> b(llvm::BasicBlock::Create(ctx_, "entry", f));
    b.CreateAlignedStore(def, f->getArg(0), llvm::Align(align_of(t)));
    b.CreateRetVoid();
    return f;
  }

  llvm::Value* emit_builtin(Call* c) {
    auto& a = c->resolved_args;
    switch (c->builtin) {
      case Builtin::SizeOf:
        return const_value(c->cv, c->type->is_untyped() ? T.s64 : c->type);
      case Builtin::TypeOf:
      case Builtin::TypeInfo:
        return type_info(c->type_arg);
      case Builtin::InitializerOf:
        return rvalue(a[0]);
      case Builtin::Print: {
        llvm::Value* len;
        llvm::Value* data = string_parts(a[0], &len);
        auto [anys, count] = print_args(c);
        llvm::Value* fd = llvm::ConstantInt::get(i32_, 1);
        if (a.back()) fd = b_.CreateSelect(cond(a.back()), llvm::ConstantInt::get(i32_, 2), fd);
        return b_.CreateCall(rt_print_, {data, len, anys, count, fd});
      }
      case Builtin::Sprint: {
        llvm::Value* len;
        llvm::Value* data = string_parts(a[0], &len);
        auto [anys, count] = print_args(c);
        auto* out = make_alloca(T.string_t, "sprint.out");
        auto [alloc_fn, allocator] = runtime_allocator(allocator_arg(c));
        b_.CreateCall(rt_sprint_, {out, data, len, anys, count, alloc_fn, allocator});
        return b_.CreateLoad(lt(T.string_t), out);
      }
      case Builtin::WriteString: {
        llvm::Value* len;
        llvm::Value* data = string_parts(a[0], &len);
        llvm::Value* fd = llvm::ConstantInt::get(i32_, 1);
        if (a[1]) fd = b_.CreateSelect(cond(a[1]), llvm::ConstantInt::get(i32_, 2), fd);
        b_.CreateCall(rt_write_, {data, len, fd});
        return nullptr;
      }
      case Builtin::Alloc:
        return alloc_bytes(c, rvalue(a[0]));
      case Builtin::Free: {
        Type* t = a[0]->type;
        llvm::Value* v = rvalue(a[0]);
        if (!t->is_pointer()) v = b_.CreateExtractValue(v, 1);
        free_bytes(v, prog_.uses_context ? allocator_arg(c) : nullptr);
        return nullptr;
      }
      case Builtin::Realloc: {
        llvm::Value* p = rvalue(a[0]);
        llvm::Value* n = rvalue(a[1]);
        llvm::Value* old = rvalue(a[2]);
        if (!prog_.uses_context) return b_.CreateCall(rt_realloc_, {p, n, old});
        return allocate(MODE_RESIZE, n, old, p, allocator_arg(c));
      }
      case Builtin::RawAlloc:
        return b_.CreateCall(rt_alloc_, {rvalue(a[0])});
      case Builtin::RawAllocAligned:
        return b_.CreateCall(rt_alloc_aligned_, {rvalue(a[0]), rvalue(a[1])});
      case Builtin::RawFree:
        b_.CreateCall(rt_free_, {rvalue(a[0])});
        return nullptr;
      case Builtin::RawRealloc:
        return b_.CreateCall(rt_realloc_, {rvalue(a[0]), rvalue(a[1]), rvalue(a[2])});
      case Builtin::New: {
        llvm::Value* p = alloc_bytes(c, llvm::ConstantInt::get(i64_, size_of(c->type_arg)), c->type_arg);
        if (!c->no_init) store_default(p, c->type_arg);
        return p;
      }
      case Builtin::NewArray: {
        Type* et = c->type_arg;
        llvm::Value* n = rvalue(a[0]);
        llvm::Value* bytes = b_.CreateMul(n, llvm::ConstantInt::get(i64_, size_of(et)));
        llvm::Value* p = alloc_bytes(c, bytes, et);
        llvm::Constant* d = default_value(et);
        if (c->no_init) {
          // left uninitialized
        } else if (d->isNullValue()) {
          b_.CreateMemSet(p, llvm::ConstantInt::get(i8_, 0), bytes, llvm::MaybeAlign(1));
        } else {
          // Initialize each element with the type's default value.
          auto* idx_a = make_alloca(T.s64, "init.i");
          b_.CreateStore(llvm::ConstantInt::get(i64_, 0), idx_a);
          auto* cond_bb = llvm::BasicBlock::Create(ctx_, "init.cond", fn_);
          auto* body_bb = llvm::BasicBlock::Create(ctx_, "init.body", fn_);
          auto* end_bb = llvm::BasicBlock::Create(ctx_, "init.end", fn_);
          b_.CreateBr(cond_bb);
          b_.SetInsertPoint(cond_bb);
          llvm::Value* i = b_.CreateLoad(i64_, idx_a);
          b_.CreateCondBr(b_.CreateICmpSLT(i, n), body_bb, end_bb);
          b_.SetInsertPoint(body_bb);
          b_.CreateStore(d, b_.CreateInBoundsGEP(lt(et), p, {i}));
          b_.CreateStore(b_.CreateAdd(i, llvm::ConstantInt::get(i64_, 1)), idx_a);
          b_.CreateBr(cond_bb);
          b_.SetInsertPoint(end_bb);
        }
        llvm::Value* view = llvm::UndefValue::get(lt(c->type));
        view = b_.CreateInsertValue(view, n, 0);
        return b_.CreateInsertValue(view, p, 1);
      }
      case Builtin::ArrayAdd: {
        Type* et = c->type_arg;
        llvm::Type* dyn_t = lt(a[0]->type->elem);
        llvm::Value* arr = rvalue(a[0]);
        llvm::Value* value = a.size() == 2 ? rvalue(a[1]) : nullptr;  // evaluate before growing
        llvm::Value* count_p = b_.CreateStructGEP(dyn_t, arr, 0);
        llvm::Value* count = b_.CreateLoad(i64_, count_p);
        llvm::Value* new_count = b_.CreateAdd(count, llvm::ConstantInt::get(i64_, 1));
        // A dynamic array keeps the allocator it first grew with (context.allocator then).
        auto [alloc_fn, ctx_allocator] = runtime_allocator(prog_.uses_context ? context_addr() : nullptr);
        auto [align, alloc_aligned] = reserve_alignment(et);
        b_.CreateCall(rt_array_reserve_, {arr, llvm::ConstantInt::get(i64_, size_of(et)), new_count,
                                          llvm::ConstantInt::get(i32_, 0), alloc_fn, ctx_allocator, align, alloc_aligned});
        llvm::Value* data = b_.CreateLoad(ptr_, b_.CreateStructGEP(dyn_t, arr, 1));
        llvm::Value* slot = b_.CreateInBoundsGEP(lt(et), data, {count});
        if (value) b_.CreateStore(value, slot);
        else store_default(slot, et);
        b_.CreateStore(new_count, count_p);
        return value ? nullptr : slot;
      }
      case Builtin::ArrayReserve: {
        auto [alloc_fn, ctx_allocator] = runtime_allocator(prog_.uses_context ? context_addr() : nullptr);
        auto [align, alloc_aligned] = reserve_alignment(c->type_arg);
        b_.CreateCall(rt_array_reserve_, {rvalue(a[0]), llvm::ConstantInt::get(i64_, size_of(c->type_arg)), rvalue(a[1]),
                                          llvm::ConstantInt::get(i32_, 1), alloc_fn, ctx_allocator, align, alloc_aligned});
        return nullptr;
      }
      case Builtin::ArrayFree: {
        Type* t = a[0]->type;
        if (!t->is_dynamic() || !prog_.uses_context) {  // a view: freed with context.allocator
          llvm::Value* v = rvalue(a[0]);
          free_bytes(b_.CreateExtractValue(v, 1), null_ptr());
          return nullptr;
        }
        llvm::Value* arr = addr_of(a[0], /*read_only=*/true);  // freed with its own allocator
        free_bytes(owned_data(lt(t), arr), b_.CreateStructGEP(lt(t), arr, 3));
        return nullptr;
      }
      case Builtin::ArrayReset: {
        llvm::Type* dyn_t = lt(a[0]->type->elem);
        llvm::Value* arr = rvalue(a[0]);
        free_bytes(owned_data(dyn_t, arr), b_.CreateStructGEP(dyn_t, arr, 3));
        // count, data and allocated; the array keeps its allocator
        b_.CreateStore(llvm::ConstantInt::get(i64_, 0), b_.CreateStructGEP(dyn_t, arr, 0));
        b_.CreateStore(null_ptr(), b_.CreateStructGEP(dyn_t, arr, 1));
        b_.CreateStore(llvm::ConstantInt::get(i64_, 0), b_.CreateStructGEP(dyn_t, arr, 2));
        return nullptr;
      }
      case Builtin::Assert: {
        llvm::Value* ok = cond(a[0]);
        auto* fail_bb = llvm::BasicBlock::Create(ctx_, "assert.fail", fn_);
        auto* ok_bb = llvm::BasicBlock::Create(ctx_, "assert.ok", fn_);
        b_.CreateCondBr(ok, ok_bb, fail_bb);
        b_.SetInsertPoint(fail_bb);
        llvm::Value* mlen = llvm::ConstantInt::get(i64_, 0);
        llvm::Value* mdata = llvm::ConstantPointerNull::get(llvm::PointerType::get(ctx_, 0));
        if (a[1]) mdata = string_parts(a[1], &mlen);
        llvm::Value *file, *flen, *line;
        if (a[2]) {  // loc = ...
          file = string_parts(a[2], &flen);
          line = b_.CreateTrunc(rvalue(a[3]), i32_);
        } else {
          const std::string& path = sm_.get(c->loc.file).path;
          file = cstring_global(path);
          flen = llvm::ConstantInt::get(i64_, path.size());
          line = llvm::ConstantInt::get(i32_, c->loc.line);
        }
        b_.CreateCall(rt_assert_, {file, flen, line, mdata, mlen});
        b_.CreateUnreachable();
        b_.SetInsertPoint(ok_bb);
        return nullptr;
      }
      case Builtin::CommandLineArguments: {
        auto* count = make_alloca_raw(i64_, "argc");
        llvm::Value* data = b_.CreateCall(rt_args_, {count});
        llvm::Value* v = llvm::UndefValue::get(lt(c->type));
        v = b_.CreateInsertValue(v, b_.CreateLoad(i64_, count), 0);
        return b_.CreateInsertValue(v, data, 1);
      }
      case Builtin::Exit:
        b_.CreateCall(rt_exit_, {rvalue(a[0])});
        b_.CreateUnreachable();
        start_dead_block();
        return nullptr;
      case Builtin::Memcpy:
        b_.CreateMemMove(rvalue(a[0]), llvm::MaybeAlign(1), rvalue(a[1]), llvm::MaybeAlign(1), rvalue(a[2]));
        return nullptr;
      case Builtin::Memset:
        b_.CreateMemSet(rvalue(a[0]), rvalue(a[1]), rvalue(a[2]), llvm::MaybeAlign(1));
        return nullptr;
      case Builtin::Min:
      case Builtin::Max: {
        llvm::Value* x = rvalue(a[0]);
        llvm::Value* y = rvalue(a[1]);
        llvm::Value* lt_ = compare(BinOp::Lt, x, y, c->type);
        return c->builtin == Builtin::Min ? b_.CreateSelect(lt_, x, y) : b_.CreateSelect(lt_, y, x);
      }
      case Builtin::Abs: {
        llvm::Value* x = rvalue(a[0]);
        if (c->type->kind == TypeKind::Float) return emit_math1(llvm::Intrinsic::fabs, x);
        if (!c->type->is_signed) return x;
        return b_.CreateSelect(b_.CreateICmpSLT(x, llvm::ConstantInt::get(x->getType(), 0)), b_.CreateNeg(x), x);
      }
      case Builtin::ToCString: {
        llvm::Value* len;
        llvm::Value* data = string_parts(a[0], &len);
        auto [alloc_fn, allocator] = runtime_allocator(null_ptr());
        return b_.CreateCall(rt_cstring_, {data, len, alloc_fn, allocator});
      }
      case Builtin::Sqrt: return emit_math1(llvm::Intrinsic::sqrt, rvalue(a[0]));
      case Builtin::Floor: return emit_math1(llvm::Intrinsic::floor, rvalue(a[0]));
      case Builtin::Ceil: return emit_math1(llvm::Intrinsic::ceil, rvalue(a[0]));
      case Builtin::Round: return emit_math1(llvm::Intrinsic::round, rvalue(a[0]));
      case Builtin::Sin: return libm_call("sin", "sinf", c->type, {rvalue(a[0])});
      case Builtin::Cos: return libm_call("cos", "cosf", c->type, {rvalue(a[0])});
      case Builtin::Tan: return libm_call("tan", "tanf", c->type, {rvalue(a[0])});
      case Builtin::Exp: return libm_call("exp", "expf", c->type, {rvalue(a[0])});
      case Builtin::Log: return libm_call("log", "logf", c->type, {rvalue(a[0])});
      case Builtin::Pow: return libm_call("pow", "powf", c->type, {rvalue(a[0]), rvalue(a[1])});
      case Builtin::Atan2: return libm_call("atan2", "atan2f", c->type, {rvalue(a[0]), rvalue(a[1])});
      case Builtin::VolatileLoad: {
        llvm::Value* p = rvalue(a[0]);
        auto* ld = b_.CreateLoad(lt(c->type), p);
        ld->setVolatile(true);
        return ld;
      }
      case Builtin::VolatileStore: {
        llvm::Value* p = rvalue(a[0]);
        llvm::Value* v = rvalue(a[1]);
        b_.CreateStore(v, p, /*isVolatile=*/true);
        return nullptr;
      }
      default:
        throw CompileError(c->loc, "internal error: unhandled built-in");
    }
  }
};

}  // namespace

std::unique_ptr<llvm::Module> generate_module(llvm::LLVMContext& ctx, const llvm::DataLayout& dl,
                                              const std::string& triple, Program& prog, TypeTable& types,
                                              const SourceManager& sm, const CodegenOptions& opts) {
  CodeGen cg(ctx, dl, triple, prog, types, sm, opts);
  return cg.run();
}

}  // namespace ojay
