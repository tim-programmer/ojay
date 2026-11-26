// Semantic analysis: name resolution, type checking, constant folding.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"

namespace llvm {
class Value;
}

namespace ojay {

// PolyProc / PolyStruct: a polymorphic procedure or struct template (`proc`
// or the decl's StructDef); only its instances are checked.
enum class SymKind { Var, Const, Type, Proc, Builtin, Library, Module, PolyProc, PolyStruct };

struct Symbol {
  std::string name;
  SymKind kind;
  SourceLoc loc;
  Scope* scope = nullptr;    // scope the symbol was declared in
  Decl* decl = nullptr;
  int decl_index = 0;        // which name of `decl`
  Type* type = nullptr;      // Var/Const: value type; Proc: proc type
  ConstValue cv;             // Const and Type symbols
  ProcLit* proc = nullptr;   // Proc symbols
  Builtin builtin = Builtin::None;
  LibraryExpr* library = nullptr;
  Scope* module_scope = nullptr;  // Module symbols (`Name :: #import "M";`)
  Expr* init = nullptr;      // global variable initializer
  bool is_global = false;
  ProcLit* owner = nullptr;  // procedure owning a local variable
  // Overloading: the symbol in the scope's table heads the set; `overloads`
  // lists every procedure of the set (itself included) once there are two.
  std::vector<Symbol*> overloads;
  enum class State { Unresolved, Resolving, Resolved } state = State::Unresolved;
  // backend
  llvm::Value* llvm_value = nullptr;
  int interp_slot = -1;  // #run interpreter: index of a local variable in its procedure's frame
  bool is_context = false;  // `context`: the Context the running procedure was given
};

// A `using` in a scope: the members of a struct variable (or of the struct a
// pointer variable points to), or the members of an enum type.
struct UsingEntry {
  Symbol* var = nullptr;
  Type* enum_type = nullptr;
};

struct Scope {
  Scope* parent = nullptr;
  ProcLit* proc = nullptr;  // enclosing procedure (null at global scope)
  std::unordered_map<std::string, Symbol*> syms;
  std::vector<UsingEntry> usings;
  bool is_module = false;        // top-level scope of the program or of a module
  std::vector<Scope*> imports;   // module scopes made visible by #import (not transitive)
};

// The main program or an imported module: its files' top-level statements.
struct ModuleUnit {
  std::string name;  // empty for the main program
  enum class Kind { Main, File, Builtin } kind;
  std::vector<Stmt*> top_level;
  Scope* scope = nullptr;  // sema: the unit's top-level scope
};

struct Program {
  std::vector<ModuleUnit> modules;  // [0] is the main program
  bool import_basic = false;
  bool import_math = false;
  bool import_hardware = false;  // ojay extension: volatile_load / volatile_store
  // Filled in by Sema:
  std::vector<ProcLit*> procs;        // every procedure literal, in discovery order
  std::vector<Symbol*> global_vars;
  std::vector<std::string> system_libraries;
  ProcLit* main_proc = nullptr;
  // The context (Preload): set once the program uses it, or anything else of
  // the Preload (Allocator, temporary_allocator, ...). Programs that never do
  // keep calling the runtime's allocator directly.
  bool uses_context = false;
  Symbol* default_context = nullptr;  // the global Context `context` starts as
  ProcLit* allocate_proc = nullptr;   // __allocate: an allocation through an Allocator (or the context's)
  Type* allocator_type = nullptr;     // Allocator
  // Runtime type information (type_info.jai): the structs codegen and the
  // #run interpreter fill in for each type's Type_Info.
  struct TypeInfoTypes {
    Type* info = nullptr;       // Type_Info
    Type* integer = nullptr;    // Type_Info_Integer
    Type* float_ = nullptr;     // Type_Info_Float
    Type* pointer = nullptr;    // Type_Info_Pointer
    Type* array = nullptr;      // Type_Info_Array
    Type* struct_ = nullptr;    // Type_Info_Struct
    Type* member = nullptr;     // Type_Info_Struct_Member
    Type* enum_ = nullptr;      // Type_Info_Enum
    Type* procedure = nullptr;  // Type_Info_Procedure
  } type_info;
  // Storage for symbols and scopes (they are referenced from the AST).
  std::vector<std::unique_ptr<Scope>> scopes;
  std::vector<std::unique_ptr<Symbol>> symbols;
};

struct SemaOptions {
  bool require_main = false;
};

// Runs semantic analysis over `prog`. Throws CompileError on the first error.
// Text inserted with `#insert` is registered with `sm` as a new source file.
void analyze(AstContext& ast, SourceManager& sm, TypeTable& types, Program& prog, const SemaOptions& opts);

}  // namespace ojay
