// Compile-time execution (`#run`). See interp.h.
//
// The interpreter follows codegen.cpp closely: the same evaluation order, the
// same integer wrap-around, the same treatment of `it` / `it_index` (re-read
// from memory after each iteration), defers run when their scope is left.
// Where the compiled program would have undefined behaviour (division by zero,
// out-of-range shifts or float-to-int conversions, invalid memory accesses) it
// reports an error instead.
#include "interp.h"

#include <pthread.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <unordered_map>

namespace ojay {

namespace {

using u64 = uint64_t;
using i64 = int64_t;

// Compile-time address space. The regions sit where none of the supported
// targets has memory-mapped registers, so a pointer to a fixed address (a
// register block) is never mistaken for compile-time memory. Everything fits
// in 32 bits, as pointers on ARM and RISC-V are 32-bit.
constexpr u64 kTypeBase = 0x6E00'0000;    // Type values: kTypeBase + 8 * (index + 1)
constexpr u64 kProcBase = 0x6F00'0000;    // procedure values: kProcBase + 8 * (index + 1)
constexpr u64 kStaticBase = 0x7000'0000;  // string literals, global variables
constexpr u64 kStaticSize = 16 << 20;
constexpr u64 kStackBase = 0x7100'0000;   // locals and temporaries
constexpr u64 kStackSize = 16 << 20;
constexpr u64 kHeapBase = 0x7200'0000;    // alloc, New, array_add, sprint

int64_t align_up(int64_t v, int64_t a) { return (v + a - 1) / a * a; }

u64 load_u(const uint8_t* p, int64_t n) {
  u64 v = 0;
  std::memcpy(&v, p, (size_t)(n < 8 ? n : 8));  // all targets (and the host) are little-endian
  return v;
}
void store_u(uint8_t* p, int64_t n, u64 v) { std::memcpy(p, &v, (size_t)(n < 8 ? n : 8)); }

u64 trunc_bits(u64 v, int bits) { return bits >= 64 ? v : v & ((u64(1) << bits) - 1); }
u64 sext(u64 v, int bits) {
  if (bits >= 64) return v;
  u64 m = u64(1) << (bits - 1);
  v = trunc_bits(v, bits);
  return (v ^ m) - m;
}

std::string hex(u64 v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)v);
  return buf;
}

// A value in target layout. Small values (scalars, strings, views) are stored
// inline.
class Val {
 public:
  Val() = default;
  explicit Val(int64_t n) { resize(n); }
  void resize(int64_t n) {
    n_ = n;
    if (n > (int64_t)sizeof small_) big_.assign((size_t)n, 0);
    else std::memset(small_, 0, (size_t)n);
  }
  int64_t size() const { return n_; }
  uint8_t* data() { return n_ > (int64_t)sizeof small_ ? big_.data() : small_; }
  const uint8_t* data() const { return n_ > (int64_t)sizeof small_ ? big_.data() : small_; }

 private:
  int64_t n_ = 0;
  alignas(8) uint8_t small_[32];
  std::vector<uint8_t> big_;
};

struct Region {
  const char* name;
  u64 base;
  u64 limit;
  std::vector<uint8_t> bytes;
};

enum class Flow { Normal, Break, Continue, Return };

// Thrown when a break, continue or return leaves a macro expansion that is
// part of an expression: it is caught by the statement being executed, which
// then passes the flow on as usual (running defers on the way).
struct FlowEscape {
  Flow flow;
};

struct Frame {
  ProcLit* proc = nullptr;  // null for the #run expression itself
  SourceLoc call_loc;
  std::vector<u64> vars;  // addresses of the locals, by Symbol::interp_slot (0: not declared yet)
  std::vector<Stmt*> defers;  // of the open scopes, innermost last
  Val ret;
};

// Runs f on a thread with a large stack (deep recursion in interpreted code
// recurses in the interpreter), unless already on one.
thread_local bool on_big_stack = false;

template <typename F>
void with_big_stack(F& f) {
  if (on_big_stack) return f();
  struct Ctx {
    F* f;
    std::exception_ptr ex;
  } ctx{&f, nullptr};
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, size_t(1) << 29);
  pthread_t th;
  void* (*tramp)(void*) = [](void* p) -> void* {
    auto* c = static_cast<Ctx*>(p);
    on_big_stack = true;
    try {
      (*c->f)();
    } catch (...) {
      c->ex = std::current_exception();
    }
    return nullptr;
  };
  int rc = pthread_create(&th, &attr, tramp, &ctx);
  pthread_attr_destroy(&attr);
  if (rc != 0) return f();  // no thread: run here
  pthread_join(th, nullptr);
  if (ctx.ex) std::rethrow_exception(ctx.ex);
}

}  // namespace

struct Interpreter::Impl {
  TypeTable& T;
  const SourceManager& sm;
  InterpHost& host;
  // Type and procedure values are ids; they are the same in every evaluation.
  std::vector<Type*> types;
  std::unordered_map<Type*, u64> type_ids;
  std::vector<ProcLit*> procs;
  std::unordered_map<ProcLit*, u64> proc_ids;
  std::unordered_map<ProcLit*, int> slot_counts;  // locals numbered so far, per procedure

  Impl(TypeTable& t, const SourceManager& s, InterpHost& h) : T(t), sm(s), host(h) {}

  u64 type_id(Type* t) {
    auto it = type_ids.find(t);
    if (it != type_ids.end()) return it->second;
    types.push_back(t);
    return type_ids[t] = kTypeBase + 8 * types.size();
  }
  Type* type_of_id(u64 v) {
    if (v <= kTypeBase || (v - kTypeBase) % 8 || (v - kTypeBase) / 8 > types.size()) return nullptr;
    return types[(v - kTypeBase) / 8 - 1];
  }
  u64 proc_id(ProcLit* p) {
    auto it = proc_ids.find(p);
    if (it != proc_ids.end()) return it->second;
    procs.push_back(p);
    return proc_ids[p] = kProcBase + 8 * procs.size();
  }
  ProcLit* proc_of_id(u64 v) {
    if (v <= kProcBase || (v - kProcBase) % 8 || (v - kProcBase) / 8 > procs.size()) return nullptr;
    return procs[(v - kProcBase) / 8 - 1];
  }
};

namespace {

// One evaluation: its memory, call stack and step count.
class Machine {
 public:
  Machine(Interpreter::Impl& in, SourceLoc run_loc)
      : in_(in), T(in.T), sm_(in.sm), ps_(in.T.target().pointer_size), run_loc_(run_loc) {
    static_ = {"static", kStaticBase, kStaticSize, {}};
    stack_ = {"stack", kStackBase, kStackSize, {}};
    heap_ = {"heap", kHeapBase, (u64)Interpreter::kMemoryLimit, {}};
    static_.bytes.reserve(4096);
    stack_.bytes.reserve(1 << 16);
  }

  ConstValue run(Expr* e, bool want_result) {
    Frame root;
    frames_.push_back(&root);
    ConstValue out;
    try {
      if (!want_result) {
        eval_any(e);
      } else {
        Val v = eval(e);
        out = read_const(v.data(), conc(e->type), "", e->loc);
      }
    } catch (const FlowEscape&) {
      err(e->loc, "internal error: break, continue or return out of a #run expression");
    }
    frames_.pop_back();
    return out;
  }

 private:
  Interpreter::Impl& in_;
  TypeTable& T;
  const SourceManager& sm_;
  int ps_;
  SourceLoc run_loc_;
  Region static_, stack_, heap_;
  std::map<u64, int64_t> heap_blocks_;  // live allocations: address -> size
  std::vector<Frame*> frames_;
  std::vector<Val> args_;  // arguments of the calls being made
  int64_t steps_ = 0;
  Stmt* flow_target_ = nullptr;  // loop targeted by the pending break / continue
  std::unordered_map<Symbol*, u64> globals_;
  std::unordered_map<std::string, u64> strings_;
  std::unordered_map<Type*, Val> defaults_;
  std::unordered_map<Type*, bool> needs_default_;
  // Macro expansions being evaluated, innermost last.
  struct ActiveExpansion {
    MacroExpansion* x;
    size_t* defer_mark;  // where the defers of the expansion's top level start
    Val* result;
  };
  std::vector<ActiveExpansion> expansions_;

  // ------------------------------------------------------------------ errors

  static std::string proc_name(const ProcLit* p) {
    return p->name.empty() ? "<anonymous procedure>" : "'" + p->name + "'";
  }

  template <typename... Args>
  [[noreturn, gnu::cold, gnu::noinline]] void err(SourceLoc loc, Args&&... args) {
    std::string msg = cat(std::forward<Args>(args)...);
    // The call stack, innermost first; the middle of a deep one is left out.
    const size_t n = frames_.size() - 1, inner = 10, outer = 4;
    for (size_t k = 0; k < n; ++k) {
      if (n > inner + outer && k == inner) {
        msg += "\n    ... (" + std::to_string(n - inner - outer) + " more calls)";
        k = n - outer - 1;
        continue;
      }
      Frame* f = frames_[n - k];
      msg += "\n    in " + proc_name(f->proc) + ", called at " + sm_.describe(f->call_loc);
    }
    msg += "\n    during compile-time execution (#run at " + sm_.describe(run_loc_) + ")";
    throw CompileError(loc, msg);
  }

  void step(SourceLoc loc) {
    if (++steps_ > Interpreter::kStepLimit)
      err(loc, "compile-time execution took more than ", Interpreter::kStepLimit,
          " steps; is there an infinite loop?");
  }

  // ------------------------------------------------------------------ types

  Type* conc(Type* t) {
    switch (t->kind) {
      case TypeKind::UntypedInt: return T.s64;
      case TypeKind::UntypedFloat: return T.f32;
      case TypeKind::Null: return T.void_ptr;
      default: return t;
    }
  }
  int64_t sz(Type* t) {
    t = conc(t);
    return t->kind == TypeKind::Void ? 0 : t->size;
  }
  int64_t al(Type* t) {
    t = conc(t);
    return t->align < 1 ? 1 : t->align;
  }
  static bool intlike(const Type* t) { return t->kind == TypeKind::Int || t->kind == TypeKind::Enum; }
  static bool ptrlike(const Type* t) {
    return t->kind == TypeKind::Pointer || t->kind == TypeKind::Proc || t->kind == TypeKind::Null ||
           t->kind == TypeKind::TypeT;
  }
  int bits(const Type* t) const {
    if (intlike(t)) return t->bits;
    if (t->kind == TypeKind::Bool) return 8;
    return ps_ * 8;
  }
  static bool sgn(const Type* t) { return intlike(t) && t->is_signed; }
  // Element size for pointer arithmetic (*void steps by bytes).
  int64_t elem_size(Type* ptr) { return ptr->elem->kind == TypeKind::Void ? 1 : sz(ptr->elem); }
  int64_t dyn_allocated_off() const { return align_up(8 + ps_, 8); }

  int64_t tuple_off(Type* tt, size_t index) {
    int64_t off = 0;
    for (size_t i = 0;; ++i) {
      off = align_up(off, al(tt->elems[i]));
      if (i == index) return off;
      off += sz(tt->elems[i]);
    }
  }

  // ------------------------------------------------------------------ memory

  u64 alloc(Region& r, int64_t size, int64_t align, SourceLoc loc) {
    u64 off = (u64)align_up((int64_t)r.bytes.size(), align);
    if (size < 0 || off + (u64)size > r.limit) {
      if (&r == &stack_) err(loc, "stack overflow during compile-time execution (more than ", kStackSize >> 20,
                             " MB of locals; is the recursion too deep?)");
      if (&r == &heap_) err(loc, "compile-time execution ran out of memory (allocating ", size,
                            " bytes; the limit is ", Interpreter::kMemoryLimit >> 20, " MB)");
      err(loc, "compile-time execution ran out of memory for static data");
    }
    r.bytes.resize(off + (u64)size);  // new bytes are zero
    return r.base + off;
  }
  u64 alloc_stack(Type* t, SourceLoc loc) { return alloc(stack_, sz(t), al(t), loc); }
  u64 heap_alloc(int64_t size, SourceLoc loc) {
    if (size < 0) err(loc, "cannot allocate a negative number of bytes (", size, ")");
    u64 a = alloc(heap_, size > 0 ? size : 1, 16, loc);
    heap_blocks_[a] = size;
    return a;
  }
  void heap_free(u64 p, SourceLoc loc) {
    if (p == 0) return;
    auto it = heap_blocks_.find(p);
    if (it == heap_blocks_.end()) err(loc, "free() of ", hex(p), ", which was not returned by alloc");
    heap_blocks_.erase(it);
  }
  u64 heap_realloc(u64 p, int64_t size, SourceLoc loc) {
    if (p == 0) return heap_alloc(size, loc);
    auto it = heap_blocks_.find(p);
    if (it == heap_blocks_.end()) err(loc, "realloc() of ", hex(p), ", which was not returned by alloc");
    int64_t old = it->second;
    u64 n = heap_alloc(size, loc);
    int64_t keep = old < size ? old : size;
    if (keep > 0) std::memmove(at(n, keep, loc), at(p, keep, loc), (size_t)keep);
    heap_blocks_.erase(p);
    return n;
  }

  // A host pointer to `n` bytes of compile-time memory at address `a`. Valid
  // until the next allocation.
  uint8_t* at(u64 a, int64_t n, SourceLoc loc) {
    u64 off = a - kStackBase;  // most accesses are to locals
    if (off <= stack_.bytes.size() && (u64)n <= stack_.bytes.size() - off) return stack_.bytes.data() + off;
    return at_slow(a, n, loc);
  }
  [[gnu::noinline]] uint8_t* at_slow(u64 a, int64_t n, SourceLoc loc) {
    for (Region* r : {&heap_, &static_})
      if (a >= r->base && a - r->base <= r->bytes.size() && (u64)n <= r->bytes.size() - (a - r->base))
        return r->bytes.data() + (a - r->base);
    if (a == 0) err(loc, "null pointer dereference");
    if (a < 4096) err(loc, "invalid memory access at address ", hex(a), " (an offset from a null pointer?)");
    if (in_.proc_of_id(a) || in_.type_of_id(a))
      err(loc, "invalid memory access at address ", hex(a), " (a procedure or Type value used as a pointer)");
    for (Region* r : {&stack_, &heap_, &static_})
      if (a >= r->base && a < r->base + r->limit)
        err(loc, "invalid memory access at address ", hex(a), " (", n, " bytes, outside the allocated ", r->name,
            " memory: a dangling or out-of-bounds pointer)");
    err(loc, "invalid memory access at address ", hex(a),
        ": compile-time code can only use memory it allocated itself (fixed addresses such as memory-mapped "
        "registers do not exist during #run)");
  }

  Val load(u64 a, Type* t, SourceLoc loc) {
    Val v(sz(t));
    if (v.size()) std::memcpy(v.data(), at(a, v.size(), loc), (size_t)v.size());
    return v;
  }
  void store(u64 a, const Val& v, SourceLoc loc) {
    if (v.size()) std::memcpy(at(a, v.size(), loc), v.data(), (size_t)v.size());
  }
  u64 load_ptr(u64 a, SourceLoc loc) { return load_u(at(a, ps_, loc), ps_); }
  i64 load_i64(u64 a, SourceLoc loc) { return (i64)load_u(at(a, 8, loc), 8); }
  void store_ptr(u64 a, u64 v, SourceLoc loc) { store_u(at(a, ps_, loc), ps_, v); }
  void store_i64(u64 a, i64 v, SourceLoc loc) { store_u(at(a, 8, loc), 8, (u64)v); }

  u64 intern(const std::string& s, SourceLoc loc) {
    auto it = strings_.find(s);
    if (it != strings_.end()) return it->second;
    u64 a = alloc(static_, (int64_t)s.size() + 1, 1, loc);
    if (!s.empty()) std::memcpy(at(a, (int64_t)s.size(), loc), s.data(), s.size());
    return strings_[s] = a;
  }

  // ------------------------------------------------------------------ scalar values

  u64 get_int(const Val& v, Type* t, int64_t off = 0) {
    t = conc(t);
    u64 raw = load_u(v.data() + off, sz(t));
    return sgn(t) ? sext(raw, bits(t)) : raw;
  }
  Val make_int(Type* t, u64 x) {
    t = conc(t);
    Val v(sz(t));
    store_u(v.data(), v.size(), trunc_bits(x, bits(t)));
    return v;
  }
  u64 get_ptr(const Val& v, int64_t off = 0) { return load_u(v.data() + off, ps_); }
  Val make_ptr(u64 p) {
    Val v(ps_);
    store_u(v.data(), ps_, p);
    return v;
  }
  double get_float(const Val& v, Type* t) {
    if (conc(t)->bits == 32) {
      float f;
      std::memcpy(&f, v.data(), 4);
      return f;
    }
    double d;
    std::memcpy(&d, v.data(), 8);
    return d;
  }
  Val make_float(Type* t, double d) {
    Val v(sz(t));
    if (conc(t)->bits == 32) {
      float f = (float)d;
      std::memcpy(v.data(), &f, 4);
    } else {
      std::memcpy(v.data(), &d, 8);
    }
    return v;
  }
  Val make_bool(bool b) {
    Val v(1);
    v.data()[0] = b ? 1 : 0;
    return v;
  }
  bool truth(const Val& v, Type* t) {
    t = conc(t);
    switch (t->kind) {
      case TypeKind::Bool: return v.data()[0] != 0;
      case TypeKind::Float: return get_float(v, t) != 0.0 || std::isnan(get_float(v, t));
      default: return load_u(v.data(), sz(t)) != 0;
    }
  }
  Val make_string(i64 count, u64 data) {
    Val v(sz(T.string_t));
    store_u(v.data(), 8, (u64)count);
    store_u(v.data() + 8, ps_, data);
    return v;
  }
  // A string or array view value: count and data.
  std::pair<i64, u64> view_parts(const Val& v) { return {(i64)load_u(v.data(), 8), load_u(v.data() + 8, ps_)}; }
  std::string read_string(const Val& v, SourceLoc loc) {
    auto [n, data] = view_parts(v);
    if (n < 0) err(loc, "string has a negative count (", n, ")");
    if (n == 0) return "";
    return std::string((const char*)at(data, n, loc), (size_t)n);
  }

  // ------------------------------------------------------------------ constants

  // Writes constant `cv` of type t into a host buffer.
  void put_const(uint8_t* p, const ConstValue& cv, Type* t, SourceLoc loc) {
    t = conc(t);
    switch (cv.kind) {
      case ConstValue::Int:
        if (t->kind == TypeKind::Float) {
          Val f = make_float(t, (double)cv.i);
          std::memcpy(p, f.data(), (size_t)f.size());
        } else if (t->kind == TypeKind::Bool) {
          p[0] = cv.i != 0;
        } else {
          store_u(p, sz(t), (u64)cv.i);
        }
        return;
      case ConstValue::Float:
        if (intlike(t)) {
          store_u(p, sz(t), (u64)(i64)cv.f);
        } else {
          Val f = make_float(t, cv.f);
          std::memcpy(p, f.data(), (size_t)f.size());
        }
        return;
      case ConstValue::Bool: p[0] = cv.b ? 1 : 0; return;
      case ConstValue::String: {
        u64 data = intern(cv.s, loc);
        store_u(p, 8, cv.s.size());
        store_u(p + 8, ps_, data);
        return;
      }
      case ConstValue::Null: store_u(p, ps_, 0); return;
      case ConstValue::Address: store_u(p, intlike(t) ? sz(t) : ps_, (u64)cv.i); return;
      case ConstValue::TypeV: store_u(p, ps_, in_.type_id(cv.t)); return;
      case ConstValue::Code: store_u(p, 8, (u64)cv.i); return;
      case ConstValue::Aggregate: {
        const auto& es = *cv.elems;
        if (es.empty()) return;  // zero value
        if (t->is_fixed_array()) {
          for (size_t i = 0; i < es.size(); ++i) put_const(p + (int64_t)i * sz(t->elem), es[i], t->elem, loc);
        } else {
          for (size_t i = 0; i < es.size() && i < t->fields.size(); ++i)
            put_const(p + t->fields[i].offset, es[i], t->fields[i].type, loc);
        }
        return;
      }
      case ConstValue::None: break;
    }
    err(loc, "internal error: constant without a value");
  }
  Val const_val(const ConstValue& cv, Type* t, SourceLoc loc) {
    Val v(sz(t));
    put_const(v.data(), cv, t, loc);
    return v;
  }

  // The result value (in a host buffer) as a constant. `path` names the part
  // being converted, for diagnostics.
  ConstValue read_const(const uint8_t* p, Type* t, const std::string& path, SourceLoc loc) {
    t = conc(t);
    auto where = [&] { return path.empty() ? std::string("the result") : "the result's " + path; };
    switch (t->kind) {
      case TypeKind::Void: return ConstValue{};
      case TypeKind::Bool: return ConstValue::of_bool(p[0] != 0);
      case TypeKind::Int:
      case TypeKind::Enum: {
        u64 raw = load_u(p, sz(t));
        return ConstValue::of_int(sgn(t) ? (__int128)(i64)sext(raw, bits(t)) : (__int128)raw);
      }
      case TypeKind::Float: {
        Val v(sz(t));
        std::memcpy(v.data(), p, (size_t)sz(t));
        return ConstValue::of_float(get_float(v, t));
      }
      case TypeKind::String: {
        i64 n = (i64)load_u(p, 8);
        u64 data = load_u(p + 8, ps_);
        if (n < 0) err(loc, where(), " is a string with a negative count (", n, ")");
        if (n == 0) return ConstValue::of_string("");
        return ConstValue::of_string(std::string((const char*)at(data, n, loc), (size_t)n));
      }
      case TypeKind::TypeT: {
        u64 v = load_u(p, ps_);
        if (v == 0) return ConstValue::null();
        Type* ty = in_.type_of_id(v);
        if (!ty) err(loc, where(), " is not a valid Type value (", hex(v), ")");
        return ConstValue::of_type(ty);
      }
      case TypeKind::Code: return ConstValue::code((__int128)load_u(p, 8));
      case TypeKind::Pointer:
      case TypeKind::Proc: {
        u64 v = load_u(p, ps_);
        if (v == 0) return ConstValue::null();
        if (t->kind == TypeKind::Proc)
          err(loc, where(), " (of type ", type_name(t), ") is a procedure; procedure values in #run results "
              "are not supported yet");
        err(loc, where(), " (of type ", type_name(t), ") is a pointer into compile-time memory, which does not "
            "exist in the compiled program; a #run result cannot contain pointers (other than null)");
      }
      case TypeKind::Array: {
        if (t->is_fixed_array()) {
          std::vector<ConstValue> es;
          es.reserve((size_t)t->count);
          for (int64_t i = 0; i < t->count; ++i)
            es.push_back(read_const(p + i * sz(t->elem), t->elem, path + "[" + std::to_string(i) + "]", loc));
          return ConstValue::aggregate(std::move(es));
        }
        bool empty = load_u(p, 8) == 0 && load_u(p + 8, ps_) == 0 &&
                     (t->is_view() || load_u(p + dyn_allocated_off(), 8) == 0);
        if (empty) return ConstValue::aggregate({});
        err(loc, where(), " (of type ", type_name(t), ") points to compile-time memory, which does not exist in "
            "the compiled program; return a fixed-size array ([N]", type_name(t->elem), ") instead");
      }
      case TypeKind::Struct: {
        if (t->is_union) err(loc, where(), " is a union (", type_name(t), "); unions in #run results are not "
                             "supported yet");
        std::vector<ConstValue> es;
        es.reserve(t->fields.size());
        for (auto& f : t->fields) {
          std::string sub = f.name.empty() ? path : (path.empty() ? f.name : path + "." + f.name);
          es.push_back(read_const(p + f.offset, f.type, sub, loc));
        }
        return ConstValue::aggregate(std::move(es));
      }
      case TypeKind::Any:
        err(loc, where(), " is an Any, which points to compile-time memory; it cannot be a #run result");
      default:
        err(loc, where(), " has type ", type_name(t), ", which cannot be a #run result");
    }
  }

  // ------------------------------------------------------------------ default values

  bool needs_default(Type* t) {
    t = conc(t);
    if (!t->is_struct() && !t->is_fixed_array()) return false;
    auto it = needs_default_.find(t);
    if (it != needs_default_.end()) return it->second;
    bool r = false;
    if (t->is_fixed_array()) {
      r = t->count > 0 && needs_default(t->elem);
    } else if (!t->is_union) {
      for (auto& f : t->fields) r |= f.default_value != nullptr || needs_default(f.type);
    }
    return needs_default_[t] = r;
  }

  // The initial value of a variable of type t, honoring struct member defaults.
  Val default_val(Type* t, SourceLoc loc) {
    t = conc(t);
    if (!needs_default(t)) return Val(sz(t));
    auto it = defaults_.find(t);
    if (it != defaults_.end()) return it->second;
    Val v(sz(t));
    if (t->is_fixed_array()) {
      Val e = default_val(t->elem, loc);
      for (int64_t i = 0; i < t->count; ++i) std::memcpy(v.data() + i * e.size(), e.data(), (size_t)e.size());
    } else {
      for (auto& f : t->fields) {
        if (f.uninit) continue;
        Val fv;
        if (f.default_value) fv = eval(f.default_value);
        else if (needs_default(f.type)) fv = default_val(f.type, loc);
        else continue;
        std::memcpy(v.data() + f.offset, fv.data(), (size_t)fv.size());
      }
    }
    defaults_[t] = v;
    return v;
  }

  // ------------------------------------------------------------------ variables

  u64 global_addr(Symbol* s, SourceLoc loc) {
    auto it = globals_.find(s);
    if (it != globals_.end()) return it->second;
    // Each evaluation starts from the initial values; writes are not kept.
    u64 a = alloc(static_, sz(s->type), al(s->type), loc);
    globals_[s] = a;
    if (s->decl && s->decl->uninit) return a;
    Val v = s->init ? eval(s->init) : default_val(s->type, loc);
    if (s->init && conc(s->init->type) != conc(s->type)) v = convert(v, s->init->type, s->type, loc);
    store(a, v, loc);
    return a;
  }

  u64 var_addr(Symbol* s, SourceLoc loc) {
    if (s->is_global) return global_addr(s, loc);
    auto& vars = frames_.back()->vars;
    if (s->interp_slot < 0 || (size_t)s->interp_slot >= vars.size() || !vars[(size_t)s->interp_slot])
      err(loc, "'", s->name, "' is a local variable of the enclosing procedure; it does not exist during "
          "compile-time execution");
    return vars[(size_t)s->interp_slot];
  }

  // Makes local variable `s` of the current procedure live at address `a`.
  void bind(Symbol* s, u64 a) {
    if (s->interp_slot < 0) s->interp_slot = in_.slot_counts[s->owner]++;
    auto& vars = frames_.back()->vars;
    if ((size_t)s->interp_slot >= vars.size()) vars.resize((size_t)s->interp_slot + 1, 0);
    vars[(size_t)s->interp_slot] = a;
  }

  // ------------------------------------------------------------------ statements

  Flow exec_scoped(Stmt* const* stmts, size_t n) {
    Frame& f = *frames_.back();
    size_t mark = stack_.bytes.size();
    size_t defer_mark = f.defers.size();
    Flow fl = Flow::Normal;
    for (size_t i = 0; i < n; ++i) {
      fl = exec(stmts[i]);
      if (fl != Flow::Normal) break;
    }
    if (f.defers.size() > defer_mark) {
      Stmt* target = flow_target_;
      for (size_t j = f.defers.size(); j-- > defer_mark;) exec_body(f.defers[j]);  // they cannot break or return
      f.defers.resize(defer_mark);
      flow_target_ = target;
    }
    stack_.bytes.resize(mark);  // the scope's locals and temporaries
    return fl;
  }
  Flow exec_scoped(const std::vector<Stmt*>& stmts) { return exec_scoped(stmts.data(), stmts.size()); }
  Flow exec_body(Stmt* s) {
    if (s->kind == NK::Block) return exec_scoped(static_cast<Block*>(s)->stmts);
    return exec_scoped(&s, 1);
  }

  // A loop's view of the flow out of its body: true if the loop must stop
  // (break out of it, or something leaving it entirely).
  bool loop_exit(Flow& fl, Stmt* loop) {
    if (fl == Flow::Normal) return false;
    if ((fl == Flow::Break || fl == Flow::Continue) && flow_target_ == loop) {
      bool brk = fl == Flow::Break;
      fl = Flow::Normal;
      return brk;
    }
    return true;
  }

  Flow exec(Stmt* s) {
    step(s->loc);
    size_t args_mark = args_.size();
    try {
      return exec_inner(s);
    } catch (const FlowEscape& fe) {  // out of a macro expansion inside this statement's expressions
      args_.resize(args_mark);
      return fe.flow;
    }
  }

  ActiveExpansion& active_expansion(MacroExpansion* x, SourceLoc loc) {
    for (size_t i = expansions_.size(); i-- > 0;)
      if (expansions_[i].x == x) return expansions_[i];
    err(loc, "internal error: macro expansion is not active");
  }

  // An inlined #expand macro: runs its statements; its value is set by a
  // `return` in it (Return::expand_target).
  Val eval_expansion(MacroExpansion* x) {
    Frame& f = *frames_.back();
    size_t mark = f.defers.size();
    Val result(sz(x->type));
    expansions_.push_back({x, &mark, &result});
    Flow fl = Flow::Normal;
    for (Stmt* st : x->body->stmts) {
      fl = exec(st);
      if (fl != Flow::Normal) break;
    }
    if (fl == Flow::Break && flow_target_ == x->body) fl = Flow::Normal;  // the macro returned
    if (f.defers.size() > mark) {
      Stmt* target = flow_target_;
      for (size_t j = f.defers.size(); j-- > mark;) exec_body(f.defers[j]);
      f.defers.resize(mark);
      flow_target_ = target;
    }
    expansions_.pop_back();
    // Locals of the expansion stay on the stack until the caller's scope ends:
    // `x := ... declares variables of that scope.
    if (fl != Flow::Normal) throw FlowEscape{fl};
    return result;
  }

  Flow exec_inner(Stmt* s) {
    switch (s->kind) {
      case NK::Block: return exec_scoped(static_cast<Block*>(s)->stmts);
      case NK::ExprStmt: {
        Expr* e = static_cast<ExprStmt*>(s)->expr;
        if (e->kind != NK::Directive) eval_any(e);  // #assert / #run: done by the checker
        return Flow::Normal;
      }
      case NK::Decl: exec_decl(static_cast<Decl*>(s)); return Flow::Normal;
      case NK::Using: return Flow::Normal;
      case NK::Assign: exec_assign(static_cast<Assign*>(s)); return Flow::Normal;
      case NK::If: {
        auto* i = static_cast<If*>(s);
        if (truth(eval(i->cond), i->cond->type)) return exec_body(i->then_s);
        if (i->else_s) return exec_body(i->else_s);
        return Flow::Normal;
      }
      case NK::Switch: return exec_switch(static_cast<Switch*>(s));
      case NK::While: {
        auto* w = static_cast<While*>(s);
        for (;;) {
          step(w->loc);
          size_t mark = stack_.bytes.size();
          bool c = truth(eval(w->cond), w->cond->type);
          stack_.bytes.resize(mark);
          if (!c) return Flow::Normal;
          Flow fl = exec_body(w->body);
          if (loop_exit(fl, w)) return fl;
        }
      }
      case NK::For: return exec_for(static_cast<For*>(s));
      case NK::Return: return exec_return(static_cast<Return*>(s));
      case NK::Break:
      case NK::Continue:
        flow_target_ = static_cast<BreakContinue*>(s)->target;
        return s->kind == NK::Break ? Flow::Break : Flow::Continue;
      case NK::Defer: {
        auto* d = static_cast<Defer*>(s);
        if (d->backtick) {  // `defer: runs when the macro caller's scope ends
          ActiveExpansion& a = active_expansion(d->target, d->loc);
          auto& defers = frames_.back()->defers;
          defers.insert(defers.begin() + (std::ptrdiff_t)*a.defer_mark, d->body);
          ++*a.defer_mark;
          return Flow::Normal;
        }
        frames_.back()->defers.push_back(d->body);
        return Flow::Normal;
      }
      case NK::Insert:  // inserted code runs in place
        for (Stmt* x : static_cast<Insert*>(s)->result->stmts) {
          Flow fl = exec(x);
          if (fl != Flow::Normal) return fl;
        }
        return Flow::Normal;
      case NK::StaticIf: {
        auto* si = static_cast<StaticIf*>(s);
        for (Stmt* x : si->taken == 1 ? si->then_s : si->else_s) {
          Flow fl = exec(x);
          if (fl != Flow::Normal) return fl;
        }
        return Flow::Normal;
      }
      default:
        err(s->loc, "internal error: unexpected statement during compile-time execution");
    }
  }

  // The values of a declaration or assignment whose right side is one
  // multi-value call, converted to the target types.
  std::vector<Val> tuple_values(Expr* call, const std::vector<Type*>& to) {
    Val tup = eval(call);
    Type* tt = call->type;
    std::vector<Val> out;
    for (size_t i = 0; i < to.size(); ++i) {
      Val v(sz(tt->elems[i]));
      std::memcpy(v.data(), tup.data() + tuple_off(tt, i), (size_t)v.size());
      out.push_back(convert(v, tt->elems[i], to[i], call->loc));
    }
    return out;
  }

  void exec_decl(Decl* d) {
    if (d->is_const) return;
    if (d->syms.size() == 1) {
      Symbol* s = d->syms[0];
      if (!d->values.empty()) {
        Val v = eval(d->values[0]);
        u64 a = alloc_stack(s->type, d->loc);
        bind(s, a);
        store(a, v, d->loc);
      } else {
        u64 a = alloc_stack(s->type, d->loc);
        bind(s, a);
        if (!d->uninit && needs_default(s->type)) store(a, default_val(s->type, d->loc), d->loc);
      }
      return;
    }
    std::vector<Val> vals;
    if (d->values.size() == 1 && d->names.size() > 1) {
      std::vector<Type*> to;
      for (Symbol* s : d->syms) to.push_back(s->type);
      vals = tuple_values(d->values[0], to);
    } else {
      for (Expr* v : d->values) vals.push_back(eval(v));
    }
    for (size_t i = 0; i < d->syms.size(); ++i) {
      Symbol* s = d->syms[i];
      u64 a = alloc_stack(s->type, d->loc);
      bind(s, a);
      if (!vals.empty()) store(a, vals[i], d->loc);
      else if (!d->uninit && needs_default(s->type)) store(a, default_val(s->type, d->loc), d->loc);
    }
  }

  void exec_assign(Assign* a) {
    if (a->overload_call) return eval_any(a->overload_call);
    if (a->op == Tok::Eq) {
      if (a->rhs.size() == 1 && a->lhs.size() > 1) {
        std::vector<u64> addrs;
        std::vector<Type*> to;
        for (Expr* l : a->lhs) {
          addrs.push_back(lval(l));
          to.push_back(l->type);
        }
        std::vector<Val> vals = tuple_values(a->rhs[0], to);
        for (size_t i = 0; i < addrs.size(); ++i) store(addrs[i], vals[i], a->loc);
        return;
      }
      if (a->lhs.size() == 1) {
        Val v = eval(a->rhs[0]);
        store(lval(a->lhs[0]), v, a->loc);
        return;
      }
      // Parallel assignment: evaluate everything first.
      std::vector<Val> vals;
      for (Expr* r : a->rhs) vals.push_back(eval(r));
      std::vector<u64> addrs;
      for (Expr* l : a->lhs) addrs.push_back(lval(l));
      for (size_t i = 0; i < vals.size(); ++i) store(addrs[i], vals[i], a->loc);
      return;
    }
    Expr* l = a->lhs[0];
    Expr* r = a->rhs[0];
    u64 addr = lval(l);
    Val cur = load(addr, l->type, a->loc);
    Val rv = eval(r);
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
    store(addr, arith(op, cur, rv, l->type, r->type, a->loc), a->loc);
  }

  bool equal(const Val& a, const Val& b, Type* t, SourceLoc loc) {
    t = conc(t);
    if (t->kind == TypeKind::String) return read_string(a, loc) == read_string(b, loc);
    if (t->kind == TypeKind::Float) return get_float(a, t) == get_float(b, t);
    if (t->kind == TypeKind::Bool) return (a.data()[0] != 0) == (b.data()[0] != 0);
    return get_int(a, t) == get_int(b, t);
  }

  Flow exec_switch(Switch* sw) {
    Val v = eval(sw->subject);
    Type* t = sw->subject->type;
    int start = -1, fallback = -1;
    for (size_t i = 0; i < sw->cases.size() && start < 0; ++i) {
      if (sw->cases[i].values.empty()) fallback = (int)i;
      for (Expr* cv : sw->cases[i].values)
        if (equal(v, eval(cv), t, cv->loc)) {
          start = (int)i;
          break;
        }
    }
    if (start < 0) start = fallback;
    if (start < 0) return Flow::Normal;
    for (size_t i = (size_t)start; i < sw->cases.size(); ++i) {
      Flow fl = exec_scoped(sw->cases[i].body);
      if (fl != Flow::Normal) return fl;
      if (!sw->cases[i].fallthrough) break;
    }
    return Flow::Normal;
  }

  Flow exec_for(For* f) {
    u64 it_a = alloc_stack(f->it_sym->type, f->loc);
    u64 idx_a = alloc_stack(T.s64, f->loc);
    bind(f->it_sym, it_a);
    if (f->index_sym) bind(f->index_sym, idx_a);
    if (f->range_lo) {
      Type* t = conc(f->range_lo->type);
      u64 lo = get_int(eval(f->range_lo), t);
      u64 hi = get_int(eval(f->range_hi), t);
      bool s = sgn(t);
      auto le = [&](u64 x, u64 y) { return s ? (i64)x <= (i64)y : x <= y; };
      if (!le(lo, hi)) return Flow::Normal;
      u64 last = f->reverse ? lo : hi;
      store(it_a, make_int(t, f->reverse ? hi : lo), f->loc);
      store_i64(idx_a, 0, f->loc);
      for (;;) {
        step(f->loc);
        Flow fl = exec_body(f->body);
        if (loop_exit(fl, f)) return fl;
        u64 cur = get_int(load(it_a, t, f->loc), t);
        if (f->reverse ? le(cur, last) : le(last, cur)) return Flow::Normal;
        store(it_a, make_int(t, f->reverse ? cur - 1 : cur + 1), f->loc);
        store_i64(idx_a, load_i64(idx_a, f->loc) + 1, f->loc);
      }
    }
    Type* at_t = conc(f->iterable->type);
    i64 count;
    u64 data;
    if (at_t->is_fixed_array()) {
      data = addr_of(f->iterable);
      count = at_t->count;
    } else {
      std::tie(count, data) = view_parts(eval(f->iterable));
    }
    int64_t esz = sz(at_t->elem);
    store_i64(idx_a, f->reverse ? count - 1 : 0, f->loc);
    for (;;) {
      i64 i = load_i64(idx_a, f->loc);
      if (f->reverse ? i < 0 : i >= count) return Flow::Normal;
      step(f->loc);
      u64 ep = data + (u64)(i * esz);
      store(it_a, f->by_pointer ? make_ptr(ep) : load(ep, at_t->elem, f->iterable->loc), f->loc);
      Flow fl = exec_body(f->body);
      if (loop_exit(fl, f)) return fl;
      i64 cur = load_i64(idx_a, f->loc);
      store_i64(idx_a, f->reverse ? cur - 1 : cur + 1, f->loc);
    }
  }

  Flow exec_return(Return* r) {
    if (r->expand_target) {  // leaves the macro expansion with its value
      ActiveExpansion& a = active_expansion(r->expand_target, r->loc);
      if (!r->values.empty()) *a.result = eval(r->values[0]);
      flow_target_ = r->expand_target->body;
      return Flow::Break;
    }
    Frame& f = *frames_.back();
    const auto& rets = f.proc->type->returns;
    if (rets.size() == 1) {
      f.ret = eval(r->values[0]);
    } else if (rets.size() > 1) {
      if (r->values.size() == 1) {
        f.ret = eval(r->values[0]);
      } else {
        Type* tt = T.tuple(rets);
        Val tup(tt->size);
        for (size_t i = 0; i < rets.size(); ++i) {
          Val v = eval(r->values[i]);
          std::memcpy(tup.data() + tuple_off(tt, i), v.data(), (size_t)v.size());
        }
        f.ret = tup;
      }
    }
    return Flow::Return;
  }

  // ------------------------------------------------------------------ addresses

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
               m->mkind == MemberKind::StringCount || m->mkind == MemberKind::StringData;
      }
      default: return false;
    }
  }

  // The address of any value: lvalues directly, other values in a temporary.
  u64 addr_of(Expr* e) {
    if (addressable(e)) return lval(e);
    Val v = eval(e);
    u64 a = alloc_stack(e->type, e->loc);
    store(a, v, e->loc);
    return a;
  }

  u64 lval(Expr* e) {
    switch (e->kind) {
      case NK::Ident: return var_addr(static_cast<Ident*>(e)->sym, e->loc);
      case NK::Unary: return get_ptr(eval(static_cast<Unary*>(e)->operand));
      case NK::Index: return index_addr(static_cast<Index*>(e));
      case NK::Member: return member_addr(static_cast<Member*>(e));
      default: break;
    }
    err(e->loc, "internal error: expression is not addressable");
  }

  u64 member_addr(Member* m) {
    u64 base = m->through_pointer ? get_ptr(eval(m->base)) : addr_of(m->base);
    Type* bt = m->through_pointer ? m->base->type->elem : m->base->type;
    switch (m->mkind) {
      case MemberKind::Field:
        for (int i : m->field_path) {
          base += (u64)bt->fields[i].offset;
          bt = bt->fields[i].type;
        }
        return base;
      case MemberKind::ArrayCount:
      case MemberKind::StringCount:
        return base;
      case MemberKind::ArrayData:
      case MemberKind::StringData:
        return base + 8;
      case MemberKind::ArrayAllocated:
        return base + (u64)dyn_allocated_off();
      default:
        err(m->loc, "internal error: member is not addressable");
    }
  }

  u64 index_addr(Index* ix) {
    Type* bt = conc(ix->base->type);
    auto checked = [&](i64 i, i64 count) {
      if ((u64)i >= (u64)count)
        err(ix->loc, "array index ", i, " is out of bounds (count is ", count, ")");
    };
    switch (bt->kind) {
      case TypeKind::Array: {
        int64_t esz = sz(bt->elem);
        if (bt->is_fixed_array()) {
          u64 base = addr_of(ix->base);
          i64 i = (i64)get_int(eval(ix->index), ix->index->type);
          checked(i, bt->count);
          return base + (u64)(i * esz);
        }
        auto [count, data] = view_parts(eval(ix->base));
        i64 i = (i64)get_int(eval(ix->index), ix->index->type);
        checked(i, count);
        return data + (u64)(i * esz);
      }
      case TypeKind::String: {
        auto [count, data] = view_parts(eval(ix->base));
        i64 i = (i64)get_int(eval(ix->index), ix->index->type);
        checked(i, count);
        return data + (u64)i;
      }
      case TypeKind::Pointer: {
        u64 p = get_ptr(eval(ix->base));
        i64 i = (i64)get_int(eval(ix->index), ix->index->type);
        return trunc_bits(p + (u64)(i * sz(bt->elem)), ps_ * 8);
      }
      default:
        err(ix->loc, "internal error: bad index base");
    }
  }

  // ------------------------------------------------------------------ expressions

  // Evaluates an expression whose value may be void or unused.
  void eval_any(Expr* e) {
    if (e->is_const()) return;
    if (e->kind == NK::Call) {
      eval_call(static_cast<Call*>(e));
      return;
    }
    eval(e);
  }

  Val eval(Expr* e) {
    if (e->is_const()) return const_val(e->cv, e->type, e->loc);
    switch (e->kind) {
      case NK::Ident: {
        Symbol* s = static_cast<Ident*>(e)->sym;
        if (s->kind == SymKind::Proc) return make_ptr(in_.proc_id(s->proc));
        if (s->kind != SymKind::Var) err(e->loc, "internal error: '", s->name, "' has no value");
        return load(var_addr(s, e->loc), s->type, e->loc);
      }
      case NK::Unary: return eval_unary(static_cast<Unary*>(e));
      case NK::Binary: return eval_binary(static_cast<Binary*>(e));
      case NK::Call: return eval_call(static_cast<Call*>(e));
      case NK::Index: return load(index_addr(static_cast<Index*>(e)), e->type, e->loc);
      case NK::Member: return eval_member(static_cast<Member*>(e));
      case NK::Cast: {
        auto* c = static_cast<Cast*>(e);
        return conversion(c->operand, c->type, c->loc);
      }
      case NK::ImplicitConv: {
        auto* ic = static_cast<ImplicitConv*>(e);
        return conversion(ic->operand, ic->type, ic->loc);
      }
      case NK::TupleGet: {
        auto* g = static_cast<TupleGet*>(e);
        Val tup = eval(g->operand);
        Val v(sz(g->type));
        std::memcpy(v.data(), tup.data() + tuple_off(g->operand->type, (size_t)g->index), (size_t)v.size());
        return v;
      }
      case NK::StructLit: {
        auto* s = static_cast<StructLit*>(e);
        Type* t = s->type;
        Val v = t->is_union ? Val(sz(t)) : default_val(t, s->loc);
        for (size_t i = 0; i < s->field_values.size(); ++i) {
          if (!s->field_values[i]) continue;
          Val fv = eval(s->field_values[i]);
          std::memcpy(v.data() + t->fields[i].offset, fv.data(), (size_t)fv.size());
        }
        return v;
      }
      case NK::ArrayLit: {
        auto* a = static_cast<ArrayLit*>(e);
        Val v(sz(a->type));
        int64_t esz = sz(a->type->elem);
        for (size_t i = 0; i < a->elems.size(); ++i) {
          Val x = eval(a->elems[i]);
          std::memcpy(v.data() + (int64_t)i * esz, x.data(), (size_t)x.size());
        }
        return v;
      }
      case NK::ProcLit: return make_ptr(in_.proc_id(static_cast<ProcLit*>(e)));
      case NK::Expansion: return eval_expansion(static_cast<MacroExpansion*>(e));
      case NK::Ifx: {
        auto* x = static_cast<Ifx*>(e);
        return truth(eval(x->cond), x->cond->type) ? eval(x->then_e) : eval(x->else_e);
      }
      case NK::Directive:
        err(e->loc, "this #run needs its own result (it is evaluated while it is being computed)");
      default:
        break;
    }
    err(e->loc, "internal error: cannot evaluate this expression at compile time");
  }

  Val eval_member(Member* m) {
    switch (m->mkind) {
      case MemberKind::Field: {
        if (m->through_pointer || addressable(m->base)) return load(member_addr(m), m->type, m->loc);
        Val b = eval(m->base);
        int64_t off = 0;
        Type* bt = m->base->type;
        for (int i : m->field_path) {
          off += bt->fields[i].offset;
          bt = bt->fields[i].type;
        }
        Val v(sz(m->type));
        std::memcpy(v.data(), b.data() + off, (size_t)v.size());
        return v;
      }
      case MemberKind::ArrayData:
        if (m->base->type->is_fixed_array() || (m->through_pointer && m->base->type->elem->is_fixed_array()))
          return m->through_pointer ? eval(m->base) : make_ptr(addr_of(m->base));
        [[fallthrough]];
      case MemberKind::ArrayCount:
      case MemberKind::ArrayAllocated:
      case MemberKind::StringCount:
      case MemberKind::StringData: {
        if (m->through_pointer) return load(member_addr(m), m->type, m->loc);
        Val b = eval(m->base);
        int64_t off = (m->mkind == MemberKind::ArrayCount || m->mkind == MemberKind::StringCount) ? 0
                      : m->mkind == MemberKind::ArrayAllocated                                    ? dyn_allocated_off()
                                                                                                  : 8;
        Val v(sz(m->type));
        std::memcpy(v.data(), b.data() + off, (size_t)v.size());
        return v;
      }
      case MemberKind::AnyType:
      case MemberKind::AnyValue: {
        Val b = eval(m->base);
        return make_ptr(get_ptr(b, m->mkind == MemberKind::AnyType ? 0 : ps_));
      }
      default:
        err(m->loc, "internal error: unresolved member");
    }
  }

  Val eval_unary(Unary* u) {
    switch (u->op) {
      case UnOp::AddrOf:
        if (u->operand->type->is_proc()) return eval(u->operand);
        return make_ptr(lval(u->operand));
      case UnOp::Deref: return load(get_ptr(eval(u->operand)), u->type, u->loc);
      case UnOp::Not: return make_bool(!truth(eval(u->operand), u->operand->type));
      case UnOp::Plus: return eval(u->operand);
      case UnOp::Neg: {
        Val v = eval(u->operand);
        Type* t = conc(u->type);
        if (t->kind == TypeKind::Float) return make_float(t, -get_float(v, t));
        return make_int(t, 0 - get_int(v, t));
      }
      case UnOp::BitNot: {
        Type* t = conc(u->type);
        return make_int(t, ~get_int(eval(u->operand), t));
      }
    }
    err(u->loc, "internal error: bad unary operator");
  }

  Val compare(BinOp op, const Val& a, const Val& b, Type* t, SourceLoc loc) {
    t = conc(t);
    if (t->kind == TypeKind::String) {
      bool eq = read_string(a, loc) == read_string(b, loc);
      return make_bool(op == BinOp::Eq ? eq : !eq);
    }
    int c;
    if (t->kind == TypeKind::Float) {
      double x = get_float(a, t), y = get_float(b, t);
      switch (op) {
        case BinOp::Eq: return make_bool(x == y);
        case BinOp::Ne: return make_bool(!(x == y));
        case BinOp::Lt: return make_bool(x < y);
        case BinOp::Le: return make_bool(x <= y);
        case BinOp::Gt: return make_bool(x > y);
        default: return make_bool(x >= y);
      }
    }
    if (t->kind == TypeKind::Bool) {
      int x = a.data()[0] != 0, y = b.data()[0] != 0;
      c = x < y ? -1 : x > y;
    } else if (sgn(t)) {
      i64 x = (i64)get_int(a, t), y = (i64)get_int(b, t);
      c = x < y ? -1 : x > y;
    } else {
      u64 x = get_int(a, t), y = get_int(b, t);
      c = x < y ? -1 : x > y;
    }
    switch (op) {
      case BinOp::Eq: return make_bool(c == 0);
      case BinOp::Ne: return make_bool(c != 0);
      case BinOp::Lt: return make_bool(c < 0);
      case BinOp::Le: return make_bool(c <= 0);
      case BinOp::Gt: return make_bool(c > 0);
      default: return make_bool(c >= 0);
    }
  }

  Val arith(BinOp op, const Val& a, const Val& b, Type* t, Type* bt, SourceLoc loc) {
    t = conc(t);
    bt = conc(bt);
    if (t->is_pointer()) {
      i64 off = (i64)get_int(b, bt);
      if (op == BinOp::Sub) off = -off;
      return make_ptr(trunc_bits(get_ptr(a) + (u64)(off * elem_size(t)), ps_ * 8));
    }
    if (op == BinOp::Shl || op == BinOp::Shr) {
      i64 amount = (i64)get_int(b, bt);
      if (amount < 0 || amount >= bits(t))
        err(loc, "shift amount ", amount, " is out of range for ", type_name(t), " (", bits(t), " bits)");
      u64 x = get_int(a, t);
      if (op == BinOp::Shl) return make_int(t, x << amount);
      return make_int(t, sgn(t) ? (u64)((i64)x >> amount) : x >> amount);
    }
    if (t->kind == TypeKind::Float) {
      if (t->bits == 32) {
        float x = (float)get_float(a, t), y = (float)get_float(b, t), r;
        switch (op) {
          case BinOp::Add: r = x + y; break;
          case BinOp::Sub: r = x - y; break;
          case BinOp::Mul: r = x * y; break;
          case BinOp::Div: r = x / y; break;
          default: err(loc, "internal error: bad float operator");
        }
        return make_float(t, r);
      }
      double x = get_float(a, t), y = get_float(b, t), r;
      switch (op) {
        case BinOp::Add: r = x + y; break;
        case BinOp::Sub: r = x - y; break;
        case BinOp::Mul: r = x * y; break;
        case BinOp::Div: r = x / y; break;
        default: err(loc, "internal error: bad float operator");
      }
      return make_float(t, r);
    }
    u64 x = get_int(a, t), y = get_int(b, t);
    bool s = sgn(t);
    switch (op) {
      case BinOp::Add: return make_int(t, x + y);
      case BinOp::Sub: return make_int(t, x - y);
      case BinOp::Mul: return make_int(t, x * y);
      case BinOp::Div:
      case BinOp::Mod: {
        if (y == 0) err(loc, op == BinOp::Div ? "division by zero" : "division by zero (in '%')");
        if (s) {
          i64 sx = (i64)x, sy = (i64)y;
          if (sy == -1 && sx == (i64)sext(u64(1) << (bits(t) - 1), bits(t)))
            err(loc, "integer overflow: ", sx, (op == BinOp::Div ? " / " : " % "), "-1 does not fit in ",
                type_name(t));
          return make_int(t, (u64)(op == BinOp::Div ? sx / sy : sx % sy));
        }
        return make_int(t, op == BinOp::Div ? x / y : x % y);
      }
      case BinOp::BitAnd: return make_int(t, x & y);
      case BinOp::BitOr: return make_int(t, x | y);
      case BinOp::BitXor: return make_int(t, x ^ y);
      default: break;
    }
    err(loc, "internal error: bad integer operator");
  }

  Val eval_binary(Binary* bin) {
    if (bin->op == BinOp::LogAnd || bin->op == BinOp::LogOr) {
      bool l = truth(eval(bin->lhs), bin->lhs->type);
      if (bin->op == BinOp::LogAnd ? !l : l) return make_bool(l);
      return make_bool(truth(eval(bin->rhs), bin->rhs->type));
    }
    Type* lt = conc(bin->lhs->type);
    Type* rt = conc(bin->rhs->type);
    Val a = eval(bin->lhs);
    Val b = eval(bin->rhs);
    switch (bin->op) {
      case BinOp::Eq: case BinOp::Ne: case BinOp::Lt: case BinOp::Le: case BinOp::Gt: case BinOp::Ge:
        return compare(bin->op, a, b, lt, bin->loc);
      default:
        break;
    }
    if (bin->op == BinOp::Sub && lt->is_pointer() && rt->is_pointer()) {
      i64 diff = (i64)(sext(get_ptr(a), ps_ * 8) - sext(get_ptr(b), ps_ * 8));
      return make_int(T.s64, (u64)(diff / elem_size(lt)));
    }
    return arith(bin->op, a, b, lt, rt, bin->loc);
  }

  // Converts the value of `e` to type `to` (casts and implicit conversions).
  Val conversion(Expr* e, Type* to, SourceLoc loc) {
    Type* from = conc(e->type);
    to = conc(to);
    if (to->kind == TypeKind::Any) {
      if (from->kind == TypeKind::Any) return eval(e);
      u64 addr = addr_of(e);
      Val any(sz(T.any_t));
      store_u(any.data(), ps_, in_.type_id(from));
      store_u(any.data() + ps_, ps_, addr);
      return any;
    }
    if (from->kind == TypeKind::Array && to->is_view()) {
      if (from->is_fixed_array()) return make_string(from->count, addr_of(e));  // a view has a string's layout
      auto [count, data] = view_parts(eval(e));
      return make_string(count, data);
    }
    return convert(eval(e), from, to, loc);
  }

  Val convert(const Val& v, Type* from, Type* to, SourceLoc loc) {
    from = conc(from);
    to = conc(to);
    if (from == to) return v;
    if (intlike(from) && intlike(to)) return make_int(to, get_int(v, from));
    if (intlike(from) && to->kind == TypeKind::Float) {
      u64 x = get_int(v, from);
      if (to->bits == 32) return make_float(to, sgn(from) ? (float)(i64)x : (float)x);
      return make_float(to, sgn(from) ? (double)(i64)x : (double)x);
    }
    if (from->kind == TypeKind::Float && intlike(to)) {
      // The value, truncated toward zero, must fit (otherwise the compiled
      // program's result would be undefined).
      double d = get_float(v, from);
      bool ok = sgn(to) ? d >= -std::ldexp(1.0, bits(to) - 1) && d < std::ldexp(1.0, bits(to) - 1)
                        : d > -1.0 && d < std::ldexp(1.0, bits(to));
      if (!ok) err(loc, "cannot convert ", d, " to ", type_name(to), " (out of range)");
      return make_int(to, sgn(to) ? (u64)(i64)d : (u64)d);
    }
    if (from->kind == TypeKind::Float && to->kind == TypeKind::Float) return make_float(to, get_float(v, from));
    if (from->kind == TypeKind::Bool && intlike(to)) return make_int(to, v.data()[0] != 0);
    if (intlike(from) && to->kind == TypeKind::Bool) return make_bool(get_int(v, from) != 0);
    if (ptrlike(from) && ptrlike(to)) return make_ptr(get_ptr(v));
    if (ptrlike(from) && intlike(to)) return make_int(to, get_ptr(v));
    if (intlike(from) && ptrlike(to)) return make_ptr(trunc_bits(get_int(v, from), ps_ * 8));
    err(loc, "internal error: unsupported conversion from ", type_name(from), " to ", type_name(to));
  }

  // ------------------------------------------------------------------ calls

  Val eval_call(Call* c) {
    if (c->builtin != Builtin::None) return eval_builtin(c);
    ProcLit* p = nullptr;
    Ident* id = c->callee->kind == NK::Ident ? static_cast<Ident*>(c->callee) : nullptr;
    if (id && id->sym->kind == SymKind::Proc) {
      p = id->sym->proc;
    } else {
      u64 f = get_ptr(eval(c->callee));
      if (f == 0) err(c->loc, "call through a null procedure pointer");
      p = in_.proc_of_id(f);
      if (!p) err(c->loc, "call through an invalid procedure pointer (", hex(f), ")");
    }
    size_t base = args_.size();
    for (Expr* a : c->resolved_args) {
      Val v = eval(a);  // may use args_ itself
      args_.push_back(std::move(v));
    }
    Val r = invoke(p, base, c->loc);
    args_.resize(base);
    return r;
  }

  // Calls p with the arguments args_[base...].
  Val invoke(ProcLit* p, size_t base, SourceLoc loc) {
    if (p->is_foreign)
      err(loc, "cannot call #foreign procedure ", proc_name(p), " at compile time (foreign code is only "
          "available in the compiled program)");
    if (!p->body) err(loc, "internal error: procedure ", proc_name(p), " has no body");
    step(loc);
    if ((int)frames_.size() > Interpreter::kMaxCallDepth)
      err(loc, "compile-time execution exceeded the call depth limit of ", Interpreter::kMaxCallDepth,
          " (infinite recursion?)");
    in_.host.prepare_proc(p);
    Frame f;
    f.proc = p;
    f.call_loc = loc;
    size_t mark = stack_.bytes.size();
    frames_.push_back(&f);
    for (size_t i = 0; i < p->params.size(); ++i) {
      Symbol* s = p->params[i]->syms[0];
      u64 a = alloc_stack(s->type, p->params[i]->loc);
      store(a, args_[base + i], loc);
      bind(s, a);
    }
    Flow fl = exec_scoped(p->body->stmts);
    if (fl != Flow::Return && !p->type->returns.empty())
      err(p->loc, "procedure ", proc_name(p), " ended without returning a value");
    frames_.pop_back();
    stack_.bytes.resize(mark);
    return std::move(f.ret);
  }

  // ------------------------------------------------------------------ built-ins

  // print / sprint: the formatted text, as the runtime's format() makes it.
  std::string format(Call* c) {
    auto& a = c->resolved_args;
    std::string fmt = read_string(eval(a[0]), a[0]->loc);
    std::vector<std::pair<Type*, u64>> args;  // the Any values
    for (size_t i = 1; i + 1 < a.size(); ++i) {
      Val any = eval(a[i]);
      args.push_back({in_.type_of_id(get_ptr(any, 0)), get_ptr(any, ps_)});
    }
    std::string out;
    size_t next = 0, start = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
      if (fmt[i] != '%') continue;
      out.append(fmt, start, i - start);
      if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
        out += '%';
        ++i;
      } else if (next < args.size()) {
        print_value(out, args[next].first, args[next].second, 0, c->loc);
        ++next;
      } else {
        out += '%';
      }
      start = i + 1;
    }
    out.append(fmt, start, std::string::npos);
    return out;
  }

  static void fmt_u64(std::string& o, u64 v) { o += std::to_string(v); }
  static void fmt_s64(std::string& o, i64 v) { o += std::to_string(v); }

  // Matches out_f64 in the runtime.
  static void fmt_f64(std::string& o, double x) {
    if (x != x) {
      o += "NaN";
      return;
    }
    if (x < 0 || (x == 0 && std::signbit(x))) {
      o += '-';
      x = -x;
    }
    if (x > 1.7976931348623157e308) {
      o += "inf";
      return;
    }
    int exp10 = 0;
    if (x >= 1e15) {
      while (x >= 10.0) {
        x /= 10.0;
        ++exp10;
      }
    }
    u64 ip = (u64)x;
    double frac = x - (double)ip;
    u64 fp = (u64)(frac * 1000000.0 + 0.5);
    if (fp >= 1000000) {
      ip += 1;
      fp -= 1000000;
    }
    fmt_u64(o, ip);
    if (fp) {
      char digits[6];
      for (int i = 5; i >= 0; --i) {
        digits[i] = (char)('0' + fp % 10);
        fp /= 10;
      }
      int n = 6;
      while (n > 0 && digits[n - 1] == '0') --n;
      o += '.';
      o.append(digits, (size_t)n);
    }
    if (exp10) {
      o += "e+";
      fmt_u64(o, (u64)exp10);
    }
  }

  void print_enum(std::string& o, Type* t, u64 addr, SourceLoc loc) {
    i64 val = (i64)get_int(load(addr, t, loc), t);
    if (!t->is_flags) {
      for (auto& m : t->members)
        if ((i64)m.value == val) {
          o += m.name;
          return;
        }
      fmt_s64(o, val);
      return;
    }
    if (val == 0) {
      for (auto& m : t->members)
        if (m.value == 0) {
          o += m.name;
          return;
        }
      o += "0";
      return;
    }
    u64 rest = (u64)val;
    bool first = true;
    for (auto& m : t->members) {
      u64 bitsv = (u64)m.value;
      if (bitsv && (rest & bitsv) == bitsv) {
        if (!first) o += " | ";
        o += m.name;
        rest &= ~bitsv;
        first = false;
      }
    }
    if (rest) {
      if (!first) o += " | ";
      fmt_u64(o, rest);
    }
  }

  // Matches print_value in the runtime.
  void print_value(std::string& o, Type* t, u64 addr, int depth, SourceLoc loc) {
    if (!t) {
      o += "(unknown)";
      return;
    }
    if (depth > 32) {
      o += "...";
      return;
    }
    t = conc(t);
    switch (t->kind) {
      case TypeKind::Void: o += "void"; return;
      case TypeKind::Bool: o += *at(addr, 1, loc) ? "true" : "false"; return;
      case TypeKind::Int: {
        u64 v = get_int(load(addr, t, loc), t);
        if (t->is_signed) fmt_s64(o, (i64)v);
        else fmt_u64(o, v);
        return;
      }
      case TypeKind::Float: fmt_f64(o, get_float(load(addr, t, loc), t)); return;
      case TypeKind::String: o += read_string(load(addr, t, loc), loc); return;
      case TypeKind::Pointer:
      case TypeKind::Proc: {
        u64 p = load_ptr(addr, loc);
        o += p ? hex(p) : "null";
        return;
      }
      case TypeKind::Array: {
        i64 count;
        u64 data;
        if (t->is_fixed_array()) {
          count = t->count;
          data = addr;
        } else {
          std::tie(count, data) = view_parts(load(addr, t, loc));
        }
        o += '[';
        for (i64 i = 0; i < count; ++i) {
          if (i) o += ", ";
          print_value(o, t->elem, data + (u64)(i * sz(t->elem)), depth + 1, loc);
        }
        o += ']';
        return;
      }
      case TypeKind::Struct: {
        o += '{';
        size_t n = t->is_union && !t->fields.empty() ? 1 : t->fields.size();
        for (size_t i = 0; i < n; ++i) {
          if (i) o += ", ";
          print_value(o, t->fields[i].type, addr + (u64)t->fields[i].offset, depth + 1, loc);
        }
        o += '}';
        return;
      }
      case TypeKind::Enum: print_enum(o, t, addr, loc); return;
      case TypeKind::Any: {
        Val any = load(addr, t, loc);
        print_value(o, in_.type_of_id(get_ptr(any, 0)), get_ptr(any, ps_), depth + 1, loc);
        return;
      }
      case TypeKind::TypeT: {
        Type* ty = in_.type_of_id(load_ptr(addr, loc));
        o += ty ? type_name(ty) : "(null type)";
        return;
      }
      default: o += "(unknown)"; return;
    }
  }

  void write_out(const std::string& s, bool to_stderr) {
    FILE* f = to_stderr ? stderr : stdout;
    std::fwrite(s.data(), 1, s.size(), f);
    std::fflush(f);
  }

  void reserve(u64 arr, int64_t esz, i64 needed, SourceLoc loc) {
    i64 allocated = load_i64(arr + (u64)dyn_allocated_off(), loc);
    if (allocated >= needed) return;
    i64 cap = allocated * 2;
    if (cap < 8) cap = 8;
    if (cap < needed) cap = needed;
    u64 data = heap_realloc(load_ptr(arr + 8, loc), cap * esz, loc);
    store_ptr(arr + 8, data, loc);
    store_i64(arr + (u64)dyn_allocated_off(), cap, loc);
  }

  Val math1(Builtin b, const Val& x, Type* t) {
    double d = get_float(x, t);
    if (conc(t)->bits == 32) {
      float f = (float)d, r = 0;
      switch (b) {
        case Builtin::Sqrt: r = std::sqrt(f); break;
        case Builtin::Floor: r = std::floor(f); break;
        case Builtin::Ceil: r = std::ceil(f); break;
        case Builtin::Round: r = std::round(f); break;
        case Builtin::Sin: r = std::sin(f); break;
        case Builtin::Cos: r = std::cos(f); break;
        case Builtin::Tan: r = std::tan(f); break;
        case Builtin::Exp: r = std::exp(f); break;
        case Builtin::Log: r = std::log(f); break;
        default: break;
      }
      return make_float(t, r);
    }
    double r = 0;
    switch (b) {
      case Builtin::Sqrt: r = std::sqrt(d); break;
      case Builtin::Floor: r = std::floor(d); break;
      case Builtin::Ceil: r = std::ceil(d); break;
      case Builtin::Round: r = std::round(d); break;
      case Builtin::Sin: r = std::sin(d); break;
      case Builtin::Cos: r = std::cos(d); break;
      case Builtin::Tan: r = std::tan(d); break;
      case Builtin::Exp: r = std::exp(d); break;
      case Builtin::Log: r = std::log(d); break;
      default: break;
    }
    return make_float(t, r);
  }

  Val eval_builtin(Call* c) {
    auto& a = c->resolved_args;
    SourceLoc loc = c->loc;
    switch (c->builtin) {
      case Builtin::Print: {
        std::string s = format(c);
        bool to_stderr = a.back() && truth(eval(a.back()), a.back()->type);
        write_out(s, to_stderr);
        return make_int(T.s64, s.size());
      }
      case Builtin::Sprint: {
        std::string s = format(c);
        u64 data = heap_alloc((int64_t)s.size() + 1, loc);
        if (!s.empty()) std::memcpy(at(data, (int64_t)s.size(), loc), s.data(), s.size());
        return make_string((i64)s.size(), data);
      }
      case Builtin::WriteString: {
        std::string s = read_string(eval(a[0]), loc);
        bool to_stderr = a[1] && truth(eval(a[1]), a[1]->type);
        write_out(s, to_stderr);
        return Val();
      }
      case Builtin::Alloc: return make_ptr(heap_alloc((i64)get_int(eval(a[0]), a[0]->type), loc));
      case Builtin::Free: {
        Val v = eval(a[0]);
        heap_free(a[0]->type->is_pointer() ? get_ptr(v) : view_parts(v).second, loc);
        return Val();
      }
      case Builtin::Realloc: {
        u64 p = get_ptr(eval(a[0]));
        i64 n = (i64)get_int(eval(a[1]), a[1]->type);
        eval(a[2]);  // the old size: known from the allocation
        return make_ptr(heap_realloc(p, n, loc));
      }
      case Builtin::New: {
        u64 p = heap_alloc(sz(c->type_arg), loc);
        if (needs_default(c->type_arg)) store(p, default_val(c->type_arg, loc), loc);
        return make_ptr(p);
      }
      case Builtin::NewArray: {
        Type* et = c->type_arg;
        i64 n = (i64)get_int(eval(a[0]), a[0]->type);
        if (n < 0) err(loc, "NewArray with a negative count (", n, ")");
        u64 p = heap_alloc(n * sz(et), loc);
        if (needs_default(et)) {
          Val d = default_val(et, loc);
          for (i64 i = 0; i < n; ++i) store(p + (u64)(i * sz(et)), d, loc);
        }
        return make_string(n, p);
      }
      case Builtin::ArrayAdd: {
        Type* et = c->type_arg;
        u64 arr = get_ptr(eval(a[0]));
        Val value;
        bool has_value = a.size() == 2;
        if (has_value) value = eval(a[1]);  // evaluated before growing
        i64 count = load_i64(arr, loc);
        reserve(arr, sz(et), count + 1, loc);
        u64 slot = load_ptr(arr + 8, loc) + (u64)(count * sz(et));
        if (has_value) store(slot, value, loc);
        else if (needs_default(et)) store(slot, default_val(et, loc), loc);
        store_i64(arr, count + 1, loc);
        return has_value ? Val() : make_ptr(slot);
      }
      case Builtin::ArrayFree: {
        heap_free(view_parts(eval(a[0])).second, loc);
        return Val();
      }
      case Builtin::ArrayReset: {
        u64 arr = get_ptr(eval(a[0]));
        heap_free(load_ptr(arr + 8, loc), loc);
        store(arr, Val(sz(a[0]->type->elem)), loc);
        return Val();
      }
      case Builtin::Assert: {
        if (truth(eval(a[0]), a[0]->type)) return Val();
        std::string msg = a.size() == 2 ? read_string(eval(a[1]), a[1]->loc) : "";
        err(loc, "assertion failed", msg.empty() ? "" : ": ", msg);
      }
      case Builtin::Exit: {
        i64 code = (i64)get_int(eval(a[0]), a[0]->type);
        err(loc, "exit(", code, ") was called during compile-time execution");
      }
      case Builtin::Memcpy: {
        u64 dst = get_ptr(eval(a[0]));
        u64 src = get_ptr(eval(a[1]));
        i64 n = (i64)get_int(eval(a[2]), a[2]->type);
        if (n < 0) err(loc, "memcpy with a negative size (", n, ")");
        if (n) std::memmove(at(dst, n, loc), at(src, n, loc), (size_t)n);
        return Val();
      }
      case Builtin::Memset: {
        u64 dst = get_ptr(eval(a[0]));
        u64 v = get_int(eval(a[1]), a[1]->type);
        i64 n = (i64)get_int(eval(a[2]), a[2]->type);
        if (n < 0) err(loc, "memset with a negative size (", n, ")");
        if (n) std::memset(at(dst, n, loc), (int)(v & 0xff), (size_t)n);
        return Val();
      }
      case Builtin::Min:
      case Builtin::Max: {
        Val x = eval(a[0]);
        Val y = eval(a[1]);
        bool lt = compare(BinOp::Lt, x, y, c->type, loc).data()[0] != 0;
        return c->builtin == Builtin::Min ? (lt ? x : y) : (lt ? y : x);
      }
      case Builtin::Abs: {
        Val x = eval(a[0]);
        Type* t = conc(c->type);
        if (t->kind == TypeKind::Float) return make_float(t, std::fabs(get_float(x, t)));
        if (!t->is_signed) return x;
        i64 v = (i64)get_int(x, t);
        return v < 0 ? make_int(t, 0 - (u64)v) : x;
      }
      case Builtin::ToCString: {
        std::string s = read_string(eval(a[0]), loc);
        u64 p = heap_alloc((int64_t)s.size() + 1, loc);
        if (!s.empty()) std::memcpy(at(p, (int64_t)s.size(), loc), s.data(), s.size());
        return make_ptr(p);
      }
      case Builtin::Sqrt: case Builtin::Floor: case Builtin::Ceil: case Builtin::Round: case Builtin::Sin:
      case Builtin::Cos: case Builtin::Tan: case Builtin::Exp: case Builtin::Log:
        return math1(c->builtin, eval(a[0]), c->type);
      case Builtin::Pow:
      case Builtin::Atan2: {
        Type* t = conc(c->type);
        double x = get_float(eval(a[0]), t), y = get_float(eval(a[1]), t);
        if (t->bits == 32) {
          float r = c->builtin == Builtin::Pow ? std::pow((float)x, (float)y) : std::atan2((float)x, (float)y);
          return make_float(t, r);
        }
        return make_float(t, c->builtin == Builtin::Pow ? std::pow(x, y) : std::atan2(x, y));
      }
      case Builtin::VolatileLoad:
      case Builtin::VolatileStore:
        err(loc, c->builtin == Builtin::VolatileLoad ? "volatile_load" : "volatile_store",
            " accesses memory-mapped hardware, which does not exist during compile-time execution");
      case Builtin::SizeOf:
        return make_int(T.s64, (u64)sz(c->type_arg));
      case Builtin::TypeOf:
        return make_ptr(in_.type_id(c->type_arg));
      default:
        err(loc, "this built-in procedure is not available during compile-time execution");
    }
  }
};

}  // namespace

Interpreter::Interpreter(TypeTable& types, const SourceManager& sm, InterpHost& host)
    : impl_(std::make_unique<Impl>(types, sm, host)) {}

Interpreter::~Interpreter() = default;

ConstValue Interpreter::run(Expr* e, SourceLoc run_loc, bool want_result) {
  ConstValue out;
  auto body = [&] {
    Machine m(*impl_, run_loc);
    out = m.run(e, want_result);
  };
  with_big_stack(body);
  return out;
}

}  // namespace ojay
