// Abstract syntax tree. Nodes are allocated in an arena owned by AstContext and
// are annotated in place by the semantic checker (types, constant values,
// resolved symbols) before code generation.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "lexer.h"
#include "support.h"
#include "types.h"

namespace llvm {
class Value;
class Function;
}  // namespace llvm

namespace ojay {

struct Scope;
struct Symbol;
struct ProcLit;

enum class NK {
  // expressions
  IntLit, FloatLit, StringLit, BoolLit, NullLit, Ident, Unary, Binary, Call, Index, Member,
  Cast, AutoCast, StructLit, ArrayLit, ProcLit, StructDef, EnumDef, PtrType, ArrType, ProcType,
  Ifx, ImplicitConv, TupleGet, Library, Directive, CodeLit, Expansion,
  // statements
  Block, ExprStmt, Decl, Assign, If, Switch, While, For, Return, Break, Continue, Defer, Import,
  Load, StaticIf, Through, Using, Insert, PushContext,
};

struct Node {
  NK kind;
  SourceLoc loc;
  Node(NK k, SourceLoc l) : kind(k), loc(l) {}
  virtual ~Node() = default;
};

// ---------------------------------------------------------------------------
// Constant values

// Address: pointer to a fixed address (i).
// Code: a `Code` value (#code); i is the checker's id of the syntax tree.
// Aggregate: a fixed array (one element per array element) or a struct (one
// element per field, in field order), e.g. the result of `#run`. An empty
// element list is the zero value of the type (used for empty array views).
// The elements are shared and never modified, so copies are cheap.
struct ConstValue {
  enum Kind { None, Int, Float, Bool, String, TypeV, Null, Address, Aggregate, Code } kind = None;
  __int128 i = 0;
  double f = 0;
  bool b = false;
  std::string s;
  Type* t = nullptr;
  std::shared_ptr<const std::vector<ConstValue>> elems;

  static ConstValue of_int(__int128 v) { ConstValue c; c.kind = Int; c.i = v; return c; }
  static ConstValue of_float(double v) { ConstValue c; c.kind = Float; c.f = v; return c; }
  static ConstValue of_bool(bool v) { ConstValue c; c.kind = Bool; c.b = v; return c; }
  static ConstValue of_string(std::string v) { ConstValue c; c.kind = String; c.s = std::move(v); return c; }
  static ConstValue of_type(Type* v) { ConstValue c; c.kind = TypeV; c.t = v; return c; }
  static ConstValue null() { ConstValue c; c.kind = Null; return c; }
  static ConstValue address(__int128 v) { ConstValue c; c.kind = Address; c.i = v; return c; }
  static ConstValue code(__int128 id) { ConstValue c; c.kind = Code; c.i = id; return c; }
  static ConstValue aggregate(std::vector<ConstValue> v) {
    ConstValue c;
    c.kind = Aggregate;
    c.elems = std::make_shared<const std::vector<ConstValue>>(std::move(v));
    return c;
  }
};

// ---------------------------------------------------------------------------
// Expressions

struct Expr : Node {
  Type* type = nullptr;  // set by sema
  ConstValue cv;         // set by sema when the expression is a compile-time constant
  Expr(NK k, SourceLoc l) : Node(k, l) {}
  bool is_const() const { return cv.kind != ConstValue::None; }
};

struct IntLit : Expr {
  unsigned __int128 value;
  IntLit(SourceLoc l, unsigned __int128 v) : Expr(NK::IntLit, l), value(v) {}
};
struct FloatLit : Expr {
  double value;
  FloatLit(SourceLoc l, double v) : Expr(NK::FloatLit, l), value(v) {}
};
struct StringLit : Expr {
  std::string value;
  bool here_string = false;  // written as #string TERM ... TERM
  StringLit(SourceLoc l, std::string v) : Expr(NK::StringLit, l), value(std::move(v)) {}
};
struct BoolLit : Expr {
  bool value;
  BoolLit(SourceLoc l, bool v) : Expr(NK::BoolLit, l), value(v) {}
};
struct NullLit : Expr {
  explicit NullLit(SourceLoc l) : Expr(NK::NullLit, l) {}
};

struct Ident : Expr {
  std::string name;
  Symbol* sym = nullptr;
  bool poly = false;  // written `$name`: introduces a type variable of a polymorphic procedure
  bool backtick = false;  // written `` `name ``: in an #expand macro, a name of the caller's scope
  Ident(SourceLoc l, std::string n) : Expr(NK::Ident, l), name(std::move(n)) {}
};

enum class UnOp { Neg, Plus, Not, BitNot, AddrOf, Deref };
struct Unary : Expr {
  UnOp op;
  Expr* operand;
  Expr* overload = nullptr;  // sema: the `operator -` call this expression stands for
  Unary(SourceLoc l, UnOp o, Expr* e) : Expr(NK::Unary, l), op(o), operand(e) {}
};

enum class BinOp {
  Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr,
  Eq, Ne, Lt, Le, Gt, Ge, LogAnd, LogOr,
};
const char* binop_str(BinOp op);

struct Binary : Expr {
  BinOp op;
  Expr* lhs;
  Expr* rhs;
  Expr* overload = nullptr;  // sema: the `operator<op>` call this expression stands for
  Binary(SourceLoc l, BinOp o, Expr* a, Expr* b) : Expr(NK::Binary, l), op(o), lhs(a), rhs(b) {}
};

struct Arg {
  std::string name;  // empty for positional
  SourceLoc name_loc;
  Expr* value = nullptr;
  bool spread = false;  // `..array`: the elements of an array as a procedure's variadic arguments
};

enum class Builtin {
  None,
  // core
  SizeOf, TypeOf, TypeInfo,
  // Basic
  Print, Sprint, WriteString, Alloc, Free, Realloc, New, NewArray, ArrayAdd, ArrayReserve, ArrayFree, ArrayReset,
  Assert, Exit, Memcpy, Memset, Min, Max, Abs, ToCString, CommandLineArguments,
  // Preload internals: the runtime's own heap, bypassing the context's allocator
  RawAlloc, RawFree, RawRealloc,
  // Math
  Sqrt, Sin, Cos, Tan, Atan2, Pow, Floor, Ceil, Round, Exp, Log,
  // Hardware (ojay extension for memory-mapped I/O)
  VolatileLoad, VolatileStore,
};

struct Call : Expr {
  Expr* callee;
  std::vector<Arg> args;
  // sema results
  Builtin builtin = Builtin::None;
  std::vector<Expr*> resolved_args;  // in parameter order; includes defaults and the vararg pack
  Type* proc_type = nullptr;
  Type* type_arg = nullptr;          // for New(T), NewArray(n, T), size_of(T)
  Expr* expansion = nullptr;         // sema: the MacroExpansion this call of an #expand macro stands for
  // Allocating built-ins: an explicit allocator (an Allocator value, e.g. `alloc(n, a)`,
  // `New(T, allocator = a)`, or temporary_allocator for tprint); null means context.allocator.
  Expr* allocator = nullptr;
  bool no_init = false;              // New / NewArray with `initialized = false`
  bool spread_args = false;          // print / sprint: resolved_args[1] is a `..array` of Any
  Call(SourceLoc l, Expr* c) : Expr(NK::Call, l), callee(c) {}
};

struct Index : Expr {
  Expr* base;
  Expr* index;
  Expr* overload = nullptr;  // sema: the `operator []` / `operator *[]` expression this stands for
  Index(SourceLoc l, Expr* b, Expr* i) : Expr(NK::Index, l), base(b), index(i) {}
};

enum class MemberKind {
  Unresolved, Field, ArrayCount, ArrayData, ArrayAllocated, StringCount, StringData,
  EnumValue, AnyType, AnyValue, ArrayAllocator,
};
struct Member : Expr {
  Expr* base;  // null for `.NAME` (enum value with inferred type)
  std::string name;
  MemberKind mkind = MemberKind::Unresolved;
  std::vector<int> field_path;  // Field: indices from the base struct, through anonymous struct/union members
  bool through_pointer = false;  // base is a pointer that is implicitly dereferenced
  Member(SourceLoc l, Expr* b, std::string n) : Expr(NK::Member, l), base(b), name(std::move(n)) {}
};

struct Cast : Expr {
  Expr* type_expr;
  Expr* operand;
  bool truncate = false;
  bool no_check = false;
  Cast(SourceLoc l, Expr* t, Expr* e) : Expr(NK::Cast, l), type_expr(t), operand(e) {}
};

struct AutoCast : Expr {  // `xx expr`
  Expr* operand;
  AutoCast(SourceLoc l, Expr* e) : Expr(NK::AutoCast, l), operand(e) {}
};

struct StructLit : Expr {
  Expr* type_expr;  // may be null (`.{...}`)
  std::vector<Arg> args;
  std::vector<Expr*> field_values;  // sema: one per field, null means default
  StructLit(SourceLoc l, Expr* t) : Expr(NK::StructLit, l), type_expr(t) {}
};

struct ArrayLit : Expr {
  Expr* elem_type_expr;  // may be null (`.[...]`)
  std::vector<Expr*> elems;
  ArrayLit(SourceLoc l, Expr* t) : Expr(NK::ArrayLit, l), elem_type_expr(t) {}
};

struct Block;
struct Decl;

struct ReturnSpec {
  std::string name;  // may be empty
  Expr* type_expr = nullptr;
  SourceLoc loc;
};

struct ProcLit : Expr {
  std::vector<Decl*> params;
  std::vector<ReturnSpec> returns;
  Block* body = nullptr;  // null for #foreign
  bool is_foreign = false;
  Expr* foreign_lib = nullptr;
  std::string foreign_name;
  bool c_call = false;
  bool variadic = false;  // last param is `..T`
  bool symmetric = false; // #symmetric: a two-parameter operator also applies with its operands swapped
  bool is_poly = false;   // has `$T` in a parameter type or a compile-time parameter (`$N: int`)
  bool poly_returns = false;  // has `$T` in a return type
  bool is_macro = false;  // #expand: inlined at each call (MacroExpansion), never called
  bool no_context = false;  // #no_context: like #c_call procedures, it has no context
  bool program_export = false;  // #program_export: an external symbol (named export_name, else name) C can call
  std::string export_name;
  Block* modify = nullptr;  // #modify { ... }: runs when a polymorphic procedure is instantiated
  std::string name;       // bound name, for diagnostics and symbol names
  // sema
  Scope* scope = nullptr;
  bool signature_done = false;
  bool body_checked = false;
  bool registered = false;  // in Program::procs: code is generated for it (not only called by #run)
  ProcLit* enclosing = nullptr;
  // codegen
  llvm::Function* llvm_fn = nullptr;
  explicit ProcLit(SourceLoc l) : Expr(NK::ProcLit, l) {}
};

struct StructDef : Expr {
  std::vector<Decl*> params;  // `struct ($T: Type, $N: int)`: a polymorphic struct
  std::vector<Decl*> members;
  bool is_union = false;
  Type* struct_type = nullptr;
  explicit StructDef(SourceLoc l) : Expr(NK::StructDef, l) {}
};

struct EnumMemberDef {
  std::string name;
  Expr* value = nullptr;
  SourceLoc loc;
};
struct EnumDef : Expr {
  Expr* base_type = nullptr;
  std::vector<EnumMemberDef> members;
  bool is_flags = false;
  Type* enum_type = nullptr;
  explicit EnumDef(SourceLoc l) : Expr(NK::EnumDef, l) {}
};

struct PtrType : Expr {
  Expr* elem;
  PtrType(SourceLoc l, Expr* e) : Expr(NK::PtrType, l), elem(e) {}
};

struct ArrType : Expr {
  ArrayKind akind;
  Expr* count;  // for fixed arrays
  Expr* elem;
  ArrType(SourceLoc l, ArrayKind k, Expr* c, Expr* e) : Expr(NK::ArrType, l), akind(k), count(c), elem(e) {}
};

struct ProcTypeExpr : Expr {
  std::vector<Expr*> params;
  std::vector<Expr*> returns;
  bool c_call = false;
  bool variadic = false;
  explicit ProcTypeExpr(SourceLoc l) : Expr(NK::ProcType, l) {}
};

struct Ifx : Expr {
  Expr* cond;
  Expr* then_e;
  Expr* else_e;
  Ifx(SourceLoc l, Expr* c, Expr* t, Expr* e) : Expr(NK::Ifx, l), cond(c), then_e(t), else_e(e) {}
};

// Inserted by sema: converts `operand` to this->type.
struct ImplicitConv : Expr {
  Expr* operand;
  ImplicitConv(SourceLoc l, Expr* e, Type* to) : Expr(NK::ImplicitConv, l), operand(e) { type = to; }
};

// Inserted by sema: extracts one value of a multi-value call.
struct TupleGet : Expr {
  Expr* operand;
  int index;
  TupleGet(SourceLoc l, Expr* e, int i) : Expr(NK::TupleGet, l), operand(e), index(i) {}
};

// `#system_library "name"` / `#library "name"`
struct LibraryExpr : Expr {
  std::string lib_name;
  bool system = false;
  LibraryExpr(SourceLoc l, std::string n, bool sys) : Expr(NK::Library, l), lib_name(std::move(n)), system(sys) {}
};

// Expression-level directives that need sema support (#run, #location, ...).
struct DirectiveExpr : Expr {
  std::string name;
  Expr* operand = nullptr;
  int module_id = -1;  // `#import` (named import): driver's index into Program::modules
  DirectiveExpr(SourceLoc l, std::string n, Expr* e) : Expr(NK::Directive, l), name(std::move(n)), operand(e) {}
};

// `#code expr` / `#code { statements }`: a compile-time value of type Code
// holding syntax that stays unchecked; #insert checks a copy where it is used.
struct CodeLit : Expr {
  Expr* code_expr = nullptr;
  Block* code_block = nullptr;
  explicit CodeLit(SourceLoc l) : Expr(NK::CodeLit, l) {}
};

// Made by sema for a call of an #expand macro: the body, inlined. `body`
// declares the parameters (evaluated once, in order) and holds a checked copy
// of the macro's statements. A plain `return` in it (Return::expand_target)
// leaves the expansion with its value; `` `return `` returns from the caller.
struct MacroExpansion : Expr {
  ProcLit* macro;
  Block* body;
  MacroExpansion(SourceLoc l, ProcLit* m, Block* b) : Expr(NK::Expansion, l), macro(m), body(b) {}
};

// ---------------------------------------------------------------------------
// Statements

struct Stmt : Node {
  Stmt(NK k, SourceLoc l) : Node(k, l) {}
};

struct Block : Stmt {
  std::vector<Stmt*> stmts;
  Scope* scope = nullptr;
  explicit Block(SourceLoc l) : Stmt(NK::Block, l) {}
};

struct ExprStmt : Stmt {
  Expr* expr;
  ExprStmt(SourceLoc l, Expr* e) : Stmt(NK::ExprStmt, l), expr(e) {}
};

struct DeclName {
  std::string name;
  SourceLoc loc;
};

struct Decl : Stmt {
  std::vector<DeclName> names;
  Expr* type_expr = nullptr;
  std::vector<Expr*> values;
  bool is_const = false;
  bool uninit = false;      // `= ---`
  bool is_using = false;
  bool is_vararg = false;   // parameter declared as `..T`
  bool is_baked = false;    // compile-time parameter `$N: int` / `$T: Type`
  bool is_global = false;
  bool backtick = false;    // `` `x := v; `` in an #expand macro: declared in the caller's scope
  bool add_context = false; // `#add_context name: T = v;`: a member added to Context
  Expr* insert = nullptr;   // a struct member `#insert expr;` (no names): members made by sema
  // sema
  std::vector<Symbol*> syms;
  Type* declared_type = nullptr;
  explicit Decl(SourceLoc l) : Stmt(NK::Decl, l) {}
};

struct Assign : Stmt {
  Tok op;  // Eq or compound assignment token
  std::vector<Expr*> lhs;
  std::vector<Expr*> rhs;
  Expr* overload_call = nullptr;  // sema: `operator +=` / `operator []=` call that replaces the assignment
  Assign(SourceLoc l, Tok o) : Stmt(NK::Assign, l), op(o) {}
};

struct If : Stmt {
  Expr* cond;
  Stmt* then_s;
  Stmt* else_s;  // may be null
  If(SourceLoc l, Expr* c, Stmt* t, Stmt* e) : Stmt(NK::If, l), cond(c), then_s(t), else_s(e) {}
};

struct SwitchCase {
  std::vector<Expr*> values;  // empty = default case
  std::vector<Stmt*> body;
  bool fallthrough = false;   // ends in #through
  SourceLoc loc;
  Scope* scope = nullptr;
};
// `if x == { case 1; ... case 2; ... case; ... }`
struct Switch : Stmt {
  Expr* subject;
  std::vector<SwitchCase> cases;
  bool complete = false;  // #complete
  Switch(SourceLoc l, Expr* s) : Stmt(NK::Switch, l), subject(s) {}
};

struct While : Stmt {
  Expr* cond;
  Stmt* body;
  While(SourceLoc l, Expr* c, Stmt* b) : Stmt(NK::While, l), cond(c), body(b) {}
};

struct For : Stmt {
  std::string it_name = "it";
  std::string index_name = "it_index";
  bool named = false;
  bool by_pointer = false;
  bool reverse = false;
  Expr* range_lo = nullptr;  // set for `a..b`
  Expr* range_hi = nullptr;
  Expr* iterable = nullptr;  // set for arrays
  Stmt* body = nullptr;
  // sema
  Symbol* it_sym = nullptr;
  Symbol* index_sym = nullptr;
  Scope* scope = nullptr;
  explicit For(SourceLoc l) : Stmt(NK::For, l) {}
};

struct Return : Stmt {
  std::vector<Expr*> values;
  bool backtick = false;                     // `` `return `` in an #expand macro: returns from the caller
  MacroExpansion* expand_target = nullptr;   // sema: this return leaves that expansion, not a procedure
  explicit Return(SourceLoc l) : Stmt(NK::Return, l) {}
};

struct BreakContinue : Stmt {
  std::string label;
  Stmt* target = nullptr;  // sema: For or While
  BreakContinue(NK k, SourceLoc l, std::string lab) : Stmt(k, l), label(std::move(lab)) {}
};

struct Defer : Stmt {
  Stmt* body;
  bool backtick = false;              // `` `defer `` in an #expand macro: runs at the end of the caller's scope
  MacroExpansion* target = nullptr;   // sema: for ``defer`, the expansion whose caller gets it
  Defer(SourceLoc l, Stmt* b) : Stmt(NK::Defer, l), body(b) {}
};

struct Import : Stmt {
  std::string module;
  int module_id = -1;  // driver: index into Program::modules
  Import(SourceLoc l, std::string m) : Stmt(NK::Import, l), module(std::move(m)) {}
};

// `using expr;` - the members of a struct variable, or of an enum type, become
// visible by their own names in the enclosing scope.
struct UsingStmt : Stmt {
  Expr* target;
  UsingStmt(SourceLoc l, Expr* t) : Stmt(NK::Using, l), target(t) {}
};

struct Load : Stmt {
  std::string path;
  Load(SourceLoc l, std::string p) : Stmt(NK::Load, l), path(std::move(p)) {}
};

// `#if cond { ... } else { ... }` - resolved at compile time.
struct StaticIf : Stmt {
  Expr* cond;
  std::vector<Stmt*> then_s;
  std::vector<Stmt*> else_s;
  int taken = -1;  // sema: 1 = then, 0 = else
  StaticIf(SourceLoc l, Expr* c) : Stmt(NK::StaticIf, l), cond(c) {}
};

// `#insert expr;` as a statement (or at top level). Sema checks the inserted
// statements into `result`, which runs in place: it opens no scope.
struct Insert : Stmt {
  Expr* operand;
  Block* result = nullptr;
  Insert(SourceLoc l, Expr* e) : Stmt(NK::Insert, l), operand(e) {}
};

// `push_context ctx { ... }`: runs the block with `ctx` (a Context) as the
// context; the previous context is restored however the block is left.
struct PushContext : Stmt {
  Expr* ctx;
  Block* body;
  PushContext(SourceLoc l, Expr* c, Block* b) : Stmt(NK::PushContext, l), ctx(c), body(b) {}
};

// ---------------------------------------------------------------------------

class AstContext;

// Deep copies of syntax trees (ast_clone.cpp), used to instantiate polymorphic
// procedures and structs. Only what the parser produces is copied: sema
// annotations (types, constant values, symbols, rewrites) are left unset.
Expr* clone_expr(AstContext& ast, const Expr* e);
Stmt* clone_stmt(AstContext& ast, const Stmt* s);

class AstContext {
 public:
  template <typename T, typename... Args>
  T* make(Args&&... args) {
    auto p = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = p.get();
    nodes_.push_back(std::move(p));
    return raw;
  }

 private:
  std::vector<std::unique_ptr<Node>> nodes_;
};

}  // namespace ojay
