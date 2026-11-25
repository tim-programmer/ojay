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
