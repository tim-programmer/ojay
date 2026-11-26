// Compile-time execution (`#run`): a tree-walking interpreter over the checked
// AST.
//
// Values live in byte buffers laid out exactly as on the target (Type::size,
// StructField::offset, the target's pointer size), so size_of, pointer
// arithmetic and casts give the same results as the compiled program, also
// when cross-compiling to a 32-bit target. Memory is a set of regions
// (static data, stack, heap) at fixed compile-time addresses; every access is
// checked, as are array bounds, division by zero and assert.
#pragma once

#include <cstdint>
#include <memory>

#include "sema.h"

namespace ojay {

// What the interpreter needs from the checker.
class InterpHost {
 public:
  virtual ~InterpHost() = default;
  // Called before procedure `p` is executed: checks its body if that has not
  // happened yet. May throw CompileError.
  virtual void prepare_proc(ProcLit* p) = 0;
  // Called before t's Type_Info is laid out: completes t (a struct's layout).
  virtual void prepare_type(Type* t) = 0;
  // The context (Preload), once the program uses it: the global holding the
  // default Context, and __allocate. Null before (allocations then go to the
  // interpreter's heap directly).
  virtual Symbol* default_context() = 0;
  virtual ProcLit* allocate_proc() = 0;
  // __allocate_aligned (New of an over-aligned type), or null.
  virtual ProcLit* allocate_aligned_proc() = 0;
  // The Type_Info_* structs (core.jai), for laying out Type_Info in
  // compile-time memory.
  virtual const Program::TypeInfoTypes& type_info_types() = 0;
  // The procedure that sets a t to its default value (initializer_of(t)), or
  // null when that is all zeros.
  virtual ProcLit* initializer_for(Type* t) = 0;
};

class Interpreter {
 public:
  Interpreter(TypeTable& types, const SourceManager& sm, InterpHost& host);
  ~Interpreter();

  // Evaluates the checked expression `e` for `#run` at `run_loc`. With
  // `want_result`, returns its value as a constant of type e->type (and fails
  // if it cannot be one, e.g. because it contains a pointer); otherwise the
  // value is discarded. Each call starts with fresh memory: global variables
  // have their initial values, and writes to them are not kept.
  // Throws CompileError on errors, with the evaluation's call stack.
  ConstValue run(Expr* e, SourceLoc run_loc, bool want_result);

  // Limits (exceeding one is a compile error).
  static constexpr int64_t kStepLimit = 50'000'000;  // statements, loop iterations and calls (a few seconds)
  static constexpr int kMaxCallDepth = 20'000;
  static constexpr int64_t kMemoryLimit = 256 << 20;   // heap bytes

  struct Impl;  // implementation (interp.cpp)

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace ojay
