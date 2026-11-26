#include "sema.h"

#include "abi.h"

#include "interp.h"
#include "parser.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <unordered_set>

namespace ojay {

extern const char* const kPreloadSource;  // preload.jai (generated preload.cpp)
extern const char* const kBasicSource;    // modules/Basic.jai (generated modules.cpp)
extern const char* const kMathSource;     // modules/Math.jai
extern const char* const kCoreSource; // core.jai

namespace {

using i128 = __int128;

bool int_fits(i128 v, const Type* t) {
  if (t->is_signed) {
    i128 lo = -((i128)1 << (t->bits - 1));
    i128 hi = ((i128)1 << (t->bits - 1)) - 1;
    return v >= lo && v <= hi;
  }
  return v >= 0 && v <= (((i128)1 << t->bits) - 1);
}

// Wraps `v` to the width and signedness of integer type `t`.
i128 int_wrap(i128 v, const Type* t) {
  unsigned __int128 mask = (((unsigned __int128)1) << t->bits) - 1;
  unsigned __int128 u = (unsigned __int128)v & mask;
  if (t->is_signed && ((u >> (t->bits - 1)) & 1)) return (i128)(u | ~mask);
  return (i128)u;
}

std::string i128_str(i128 v) {
  if (v == 0) return "0";
  bool neg = v < 0;
  unsigned __int128 u = neg ? (unsigned __int128)(-(v + 1)) + 1 : (unsigned __int128)v;
  std::string s;
  while (u) {
    s += char('0' + (int)(u % 10));
    u /= 10;
  }
  if (neg) s += '-';
  std::reverse(s.begin(), s.end());
  return s;
}

bool is_inferred_member(const Expr* e) {
  return e->kind == NK::Member && static_cast<const Member*>(e)->base == nullptr;
}

// Types whose values may cross a C calling-convention boundary as-is.
bool is_c_scalar(const Type* t) {
  switch (t->kind) {
    case TypeKind::Int: case TypeKind::Float: case TypeKind::Bool: case TypeKind::Pointer:
    case TypeKind::Enum: case TypeKind::Proc:
      return true;
    default:
      return false;
  }
}

// An #expand macro being expanded.
struct MacroCtx {
  int id = 0;
  MacroExpansion* exp = nullptr;
  Scope* caller_scope = nullptr;       // where backtick names are looked up and declared
  bool caller_in_run_operand = false;  // the caller is the expression of a #run
  int caller_defer_depth = 0;
  Scope* body_scope = nullptr;         // the scope of the expansion's top level
  // The expansion of a for_expansion for `for x, i: ...`: its `it / `it_index declare x and i.
  std::string rename_it, rename_index;
};

struct Context {
  Scope* scope;
  ProcLit* proc;
  std::vector<Stmt*> loops;
  int defer_depth;
  std::vector<MacroCtx> macros;
  int push_depth;
};

// An error that needs no notes on where it happened (it says so itself).
struct FinalError : CompileError {
  using CompileError::CompileError;
};

// A `#code` value: unchecked syntax, and where it was written.
struct CodeEntry {
  Expr* expr = nullptr;
  Block* block = nullptr;
  SourceLoc loc;
  Scope* scope = nullptr;
  int macro_id = 0;  // the expansion it was written in (0: none)
};

class Checker : public InterpHost {
 public:
  Checker(AstContext& ast, SourceManager& sm, TypeTable& types, Program& prog, const SemaOptions& opts)
      : ast_(ast), sm_(sm), T(types), prog_(prog), opts_(opts), interp_(types, sm, *this) {}

  void run() {
    universe_ = new_scope(nullptr, nullptr);
    setup_universe();
    setup_type_info();
    setup_preload();
    for (auto& m : prog_.modules) {
      m.scope = new_scope(universe_, nullptr);
      m.scope->is_module = true;
      if (m.kind == ModuleUnit::Kind::Builtin) add_module_builtins(m.scope, m.name);
    }
    for (auto& m : prog_.modules)
      if (m.kind == ModuleUnit::Kind::Builtin) add_module_source(m.scope, m.name);
    global_ = prog_.modules[0].scope;
    // Imported modules first (they come later in the list), so #if conditions
    // can use the constants of the modules a unit imports.
    for (size_t i = prog_.modules.size(); i-- > 0;) {
      if (prog_.modules[i].kind == ModuleUnit::Kind::Builtin) continue;
      scope_ = prog_.modules[i].scope;
      collect(prog_.modules[i].top_level, false);
      // The Preload is visible in every module, after the module's own imports.
      scope_->imports.push_back(preload_);
      scope_->imports.push_back(core_);
    }
    scope_ = global_;

    for (size_t i = 0; i < global_order_.size(); ++i) resolve(global_order_[i]);
    // Top-level #assert and #run, in order.
    for (auto& [stmt, sc] : top_directives_) {
      scope_ = sc;
      check_directive_stmt(stmt);
    }
    scope_ = global_;
    // Laying out a struct can register procedures (its constants), and
    // checking a body can make structs: until neither has more to do.
    for (size_t ns = 0, np = 0; ns < all_structs_.size() || np < pending_.size();) {
      for (; ns < all_structs_.size(); ++ns) complete_type(all_structs_[ns], SourceLoc{});
      for (; np < pending_.size(); ++np) check_proc_body(pending_[np]);
    }

    // `main` is optional (an object may be a library); when present it becomes
    // the program entry point.
    auto it = global_->syms.find("main");
    if (it == global_->syms.end()) {
      if (opts_.require_main) fail(SourceLoc{}, "no 'main' procedure found");
    } else {
      Symbol* m = it->second;
      if (m->kind != SymKind::Proc) fail(m->loc, "'main' must be a procedure");
      if (!m->type->params.empty() || !m->type->returns.empty())
        fail(m->loc, "'main' must take no arguments and return nothing: main :: () { ... }");
      prog_.main_proc = m->proc;
    }
  }

 private:
  AstContext& ast_;
  SourceManager& sm_;
  TypeTable& T;
  Program& prog_;
  SemaOptions opts_;

  Scope* universe_ = nullptr;
  Scope* global_ = nullptr;
  Scope* scope_ = nullptr;
  ProcLit* proc_ = nullptr;
  std::vector<Stmt*> loops_;
  int defer_depth_ = 0;
  // Metaprogramming: the #expand macros being expanded (innermost last) and
  // the #code values (ids are index + 1).
  std::vector<MacroCtx> macros_;
  int next_macro_id_ = 0;
  std::vector<CodeEntry> codes_;
  static constexpr size_t kMaxMacroDepth = 100;

  std::vector<Symbol*> global_order_;
  std::vector<std::pair<Stmt*, Scope*>> top_directives_;
  std::map<std::pair<Scope*, uint32_t>, Scope*> file_scopes_;  // (unit scope, file) -> the file's scope
  Scope* cur_file_scope_ = nullptr;  // collect: the file scope of nested statements
  Type* lambda_hint_ = nullptr;      // define_proc: the procedure type a short lambda is used as
  bool lambda_hint_results_ = true;  // ... including its results (else they are inferred)
  // The Preload (preload.jai): its declarations are resolved only when used.
  Scope* preload_ = nullptr;
  Scope* core_ = nullptr;           // core.jai: For_Flags, Type_Info and the Type_Info_* structs
  Type* for_flags_t_ = nullptr;     // For_Flags
  Type* source_loc_t_ = nullptr;    // Source_Code_Location
  bool in_param_default_ = false;   // checking a parameter's default value (#caller_location is allowed)
  // Backtick declarations nested inside a macro body (e.g. `it in a
  // for_expansion's loop): Code from the caller inserted where they are
  // visible sees them.
  std::vector<std::pair<int, Symbol*>> nested_backticks_;  // (macro id, symbol)
  // `remove` targets: the loops being checked (innermost last). A for over
  // a value with a for_expansion has `inserts_depth`: the #insert (remove = ...)
  // that inserted its body comes after it in removes_.
  struct RemoveTarget {
    For* loop;
    bool expanded;
    size_t inserts_depth;
  };
  std::vector<RemoveTarget> remove_targets_;
  struct InsertRemove {
    Block* code;
    Context ctx;  // where the #insert is (the macro's context)
    bool in_run_operand;
  };
  std::vector<InsertRemove> insert_removes_;
  // Set while a for_expansion call is being checked: the loop and its iterator names.
  For* pending_for_ = nullptr;
  std::unordered_map<For*, MacroExpansion*> for_expansions_;  // a for over a value -> its expansion
  Symbol* context_sym_ = nullptr;   // `context`
  bool lazy_collect_ = false;       // collecting the Preload: declarations are not resolved eagerly
  bool context_ready_ = false;
  bool context_resolving_ = false;
  Type* context_type_ = nullptr;
  std::vector<std::pair<Decl*, Scope*>> context_additions_;  // #add_context members
  int push_depth_ = 0;  // push_context blocks around the code being checked (in the current procedure)
  std::vector<ProcLit*> pending_;
  std::unordered_map<Type*, std::pair<StructDef*, Scope*>> struct_defs_;
  std::vector<Type*> all_structs_;
  std::unordered_set<std::string> libs_seen_;
  // gather_procs: how far each candidate is from the use (its own scopes
  // before their imports before outer scopes); equally good overloads are
  // decided in favor of the nearest.
  std::unordered_map<Symbol*, int> proc_distance_;

  // Compile-time execution. Code checked for #run only (the operand of a #run,
  // and the bodies of procedures that only #run calls) is in compile-time
  // mode: the procedures it uses are checked but not registered for code
  // generation. Inside the body of an unregistered procedure (`comptime_owner_`)
  // their registration is deferred until that procedure is registered, which
  // happens if runtime code uses it too.
  Interpreter interp_;
  bool comptime_ = false;
  ProcLit* comptime_owner_ = nullptr;
  bool in_run_operand_ = false;  // checking the expression of a #run itself
  std::unordered_map<ProcLit*, std::vector<ProcLit*>> deferred_regs_;

  struct ComptimeMode {
    Checker& c;
    bool comptime;
    ProcLit* owner;
    bool in_operand;
    ComptimeMode(Checker& ch, bool ct, ProcLit* own, bool operand)
        : c(ch), comptime(ch.comptime_), owner(ch.comptime_owner_), in_operand(ch.in_run_operand_) {
      c.comptime_ = ct;
      c.comptime_owner_ = own;
      c.in_run_operand_ = operand;
    }
    ~ComptimeMode() {
      c.comptime_ = comptime;
      c.comptime_owner_ = owner;
      c.in_run_operand_ = in_operand;
    }
  };

  // Registers `p` for body checking and code generation, with the procedures
  // its body needed while it was checked for compile-time execution only.
  void register_proc(ProcLit* p) {
    if (p->registered || p->is_macro) return;  // macros are expanded, never called
    p->registered = true;
    prog_.procs.push_back(p);
    if (p->body) pending_.push_back(p);
    auto it = deferred_regs_.find(p);
    if (it == deferred_regs_.end()) return;
    std::vector<ProcLit*> deps = std::move(it->second);
    deferred_regs_.erase(it);
    for (ProcLit* d : deps) register_proc(d);
  }

  // `p` is used by the code being checked.
  void want_proc(ProcLit* p) {
    if (p->registered || p->is_macro) return;
    if (!comptime_) return register_proc(p);
    if (comptime_owner_ && comptime_owner_ != p) {
      auto& deps = deferred_regs_[comptime_owner_];
      if (std::find(deps.begin(), deps.end(), p) == deps.end()) deps.push_back(p);
    }
  }

 public:
  // InterpHost: checks a procedure's body before #run executes it.
  void prepare_proc(ProcLit* p) override {
    if (p->body_checked || !p->body) return;
    ComptimeMode mode(*this, !p->registered, p, false);
    check_proc_body(p);
  }
  void prepare_type(Type* t) override { complete_type(t, SourceLoc{}); }
  Symbol* default_context() override { return prog_.default_context; }
  ProcLit* allocate_proc() override { return prog_.allocate_proc; }
  ProcLit* allocate_aligned_proc() override { return prog_.allocate_aligned_proc; }
  const Program::TypeInfoTypes& type_info_types() override { return prog_.type_info; }

 private:

  // ------------------------------------------------------------------ scopes

  Scope* new_scope(Scope* parent, ProcLit* proc) {
    prog_.scopes.push_back(std::make_unique<Scope>());
    Scope* s = prog_.scopes.back().get();
    s->parent = parent;
    s->proc = proc;
    return s;
  }

  Symbol* new_symbol(const std::string& name, SymKind k, SourceLoc loc) {
    prog_.symbols.push_back(std::make_unique<Symbol>());
    Symbol* s = prog_.symbols.back().get();
    s->name = name;
    s->kind = k;
    s->loc = loc;
    return s;
  }

  static bool is_proc_decl(const Decl* d, size_t i) {
    return d && d->is_const && d->values.size() == d->names.size() && d->values[i]->kind == NK::ProcLit;
  }

  // Declares a constant from `d` (name `i`); procedures with the same name
  // form an overload set.
  Symbol* declare_const(Scope* sc, Decl* d, size_t i) {
    const std::string& name = d->names[i].name;
    auto it = sc->syms.find(name);
    if (it != sc->syms.end() && is_proc_decl(d, i) && is_proc_decl(it->second->decl, it->second->decl_index)) {
      Symbol* head = it->second;
      Symbol* s = new_symbol(name, SymKind::Const, d->names[i].loc);
      s->scope = sc;
      s->is_global = head->is_global;
      if (head->overloads.empty()) head->overloads.push_back(head);
      head->overloads.push_back(s);
      return s;
    }
    return declare(sc, name, SymKind::Const, d->names[i].loc);
  }

  Symbol* declare(Scope* sc, const std::string& name, SymKind k, SourceLoc loc) {
    auto it = sc->syms.find(name);
    if (it != sc->syms.end()) {
      fail(loc, "redeclaration of '", name, "' (previous declaration at ", sm_.describe(it->second->loc), ")");
    }
    Symbol* s = new_symbol(name, k, loc);
    s->scope = sc;
    s->is_global = sc->is_module || sc == universe_;
    sc->syms[name] = s;
    return s;
  }

  Context save() { return Context{scope_, proc_, loops_, defer_depth_, macros_, push_depth_}; }
  void restore(const Context& c) {
    scope_ = c.scope;
    proc_ = c.proc;
    loops_ = c.loops;
    defer_depth_ = c.defer_depth;
    macros_ = c.macros;
    push_depth_ = c.push_depth;
  }

  // Restores the context (and whether a #run expression is being checked)
  // when it goes out of scope.
  struct ContextGuard {
    Checker& c;
    Context ctx;
    bool in_run_operand;
    explicit ContextGuard(Checker& ch) : c(ch), ctx(ch.save()), in_run_operand(ch.in_run_operand_) {}
    ~ContextGuard() {
      c.restore(ctx);
      c.in_run_operand_ = in_run_operand;
    }
  };

  // Runs f; an error from it gets `note` (a line saying where the code came from).
  template <typename F>
  auto with_note(const std::string& note, F f) {
    try {
      return f();
    } catch (const FinalError&) {
      throw;
    } catch (const CompileError& e) {
      throw CompileError(e.loc, cat(e.what(), "\n    ", note));
    }
  }

  std::string insert_note(SourceLoc at) { return "in code inserted at " + sm_.describe(at); }


  // What a name refers to: a symbol, a member of a `using` variable, or a
  // member of a `using` enum.
  struct Found {
    Symbol* sym = nullptr;
    Symbol* via = nullptr;       // the name is a member of this variable's struct
    Type* enum_type = nullptr;   // the name is a member of this enum
  };

  void check_capture(Symbol* sym, const std::string& name, SourceLoc loc) {
    if (sym->kind == SymKind::Var && !sym->is_global && sym->owner != proc_)
      fail(loc, "cannot use local variable '", name, "' of an enclosing procedure (Jai procedures do not capture)");
  }

  Found find(const std::string& name, SourceLoc loc) {
    Found f;
    for (Scope* s = scope_; s; s = s->parent) {
      auto it = s->syms.find(name);
      if (it != s->syms.end()) {
        check_capture(it->second, name, loc);
        f.sym = it->second;
        return f;
      }
      for (auto u = s->usings.rbegin(); u != s->usings.rend(); ++u) {
        if (u->var) {
          Type* t = u->var->type->is_pointer() ? u->var->type->elem : u->var->type;
          std::vector<int> path;
          if (t->kind == TypeKind::Struct && find_field(t, name, path)) {
            check_capture(u->var, name, loc);
            f.via = u->var;
            return f;
          }
        } else {
          for (auto& m : u->enum_type->members)
            if (m.name == name) {
              f.enum_type = u->enum_type;
              return f;
            }
        }
      }
      for (Scope* im : s->imports) {
        auto jt = im->syms.find(name);
        if (jt != im->syms.end()) {
          if (Symbol* e = exported_sym(jt->second)) {
            f.sym = e;
            return f;
          }
        }
      }
    }
    return f;
  }

  // Every procedure named `name` visible from the current scope (the overloads
  // in each enclosing scope and imported module), and the first built-in with
  // that name. A non-procedure declaration hides the procedures outside it.
  void gather_procs(const std::string& name, std::vector<Symbol*>& procs, Builtin& builtin) {
    proc_distance_.clear();
    int distance = 0;
    auto add = [&](Symbol* s, bool imported = false) {
      if (s->kind == SymKind::Builtin) {
        if (builtin == Builtin::None) builtin = s->builtin;
        return true;
      }
      if (imported && !exported_sym(s)) return true;
      if (!is_proc_decl(s->decl, s->decl_index)) return false;
      for (Symbol* o : s->overloads.empty() ? std::vector<Symbol*>{s} : s->overloads)
        if ((!imported || exported(o)) && std::find(procs.begin(), procs.end(), o) == procs.end()) {
          procs.push_back(o);
          proc_distance_[o] = distance;
        }
      return true;
    };
    for (Scope* sc = scope_; sc; sc = sc->parent) {
      auto it = sc->syms.find(name);
      if (it != sc->syms.end() && !add(it->second)) return;
      ++distance;
      for (Scope* im : sc->imports) {
        auto jt = im->syms.find(name);
        if (jt != im->syms.end()) add(jt->second, /*imported=*/true);
      }
      ++distance;
    }
  }

  // The module scope `e` names, if it is the name of a named import.
  Scope* module_of(Expr* e) {
    if (e->kind != NK::Ident) return nullptr;
    auto* id = static_cast<Ident*>(e);
    Symbol* s = id->sym ? id->sym : find(id->name, id->loc).sym;
    if (!s || !s->decl || !s->decl->is_const) return nullptr;
    resolve(s);
    return s->kind == SymKind::Module ? s->module_scope : nullptr;
  }

  Symbol* module_member(Scope* ms, Member* m) {
    auto it = ms->syms.find(m->name);
    Symbol* s = it == ms->syms.end() ? nullptr : exported_sym(it->second);
    if (!s) fail(m->loc, "module '", static_cast<Ident*>(m->base)->name, "' has no member '", m->name, "'",
                 it == ms->syms.end() ? "" : " that it exports (it is declared after #scope_module or #scope_file)");
    return s;
  }

  Symbol* lookup(const std::string& name, SourceLoc loc) {
    Found f = find(name, loc);
    if (f.sym) return f.sym;
    if (f.via || f.enum_type) fail(loc, "'", name, "' (brought in by 'using') cannot be used here");
    for (Scope* sc = scope_; sc; sc = sc->parent)
      for (Scope* im : sc->imports)
        if (im->syms.count(name))
          fail(loc, "undeclared identifier '", name, "' (an imported module declares it after #scope_module or "
               "#scope_file, so it is private to that module)");
    if (is_basic_name(name)) {
      fail(loc, "undeclared identifier '", name, "' (did you forget #import \"Basic\"?)");
    }
    if (name == "context")
      fail(loc, "the context (context.allocator, context.logger, ...) is not supported yet");
    if (name == "compiler_get_nodes" || name == "Code_Node" || name.rfind("Code_", 0) == 0 ||
        name == "code_of" || name == "compiler_get_code")
      fail(loc, "'", name, "': Code_Node introspection (compiler_get_nodes) is not supported yet");
    if (name == "volatile_load" || name == "volatile_store") {
      fail(loc, "undeclared identifier '", name, "' (did you forget #import \"Hardware\"?)");
    }
    if (is_math_name(name)) {
      fail(loc, "undeclared identifier '", name, "' (did you forget #import \"Math\"?)");
    }
    fail(loc, "undeclared identifier '", name, "'");
  }

  // ------------------------------------------------------------------ #location / #caller_location

  // `loc` as a Source_Code_Location constant. Text made by #insert has no file
  // of its own: it reports where it was inserted. Files report an absolute
  // path; the embedded sources keep their names (<Basic>, <core>, ...).
  ConstValue location_value(SourceLoc loc) {
    while (loc.file < sm_.count() && sm_.get(loc.file).origin.line != 0) loc = sm_.get(loc.file).origin;
    std::string path = loc.file < sm_.count() ? sm_.get(loc.file).path : std::string();
    if (!path.empty() && path[0] != '<') {
      std::error_code ec;
      std::filesystem::path abs = std::filesystem::absolute(path, ec);
      if (!ec) path = abs.lexically_normal().string();
    }
    return ConstValue::aggregate({ConstValue::of_string(path), ConstValue::of_int(loc.line), ConstValue::of_int(loc.col)});
  }

  static bool is_caller_location(const Expr* e) {
    return e && e->kind == NK::Directive && static_cast<const DirectiveExpr*>(e)->name == "caller_location";
  }

  // The argument a call at `loc` passes for a `#caller_location` default.
  Expr* caller_location_at(SourceLoc loc) {
    auto* e = ast_.make<DirectiveExpr>(loc, "caller_location", nullptr);
    e->type = source_loc_t_;
    e->cv = location_value(loc);
    return e;
  }

  // `#location(x)`: where the declaration `x` names is (a procedure, variable,
  // constant, type or module member); any other expression gives its own location.
  Type* check_location(DirectiveExpr* d) {
    Expr* x = d->operand;
    Symbol* sym = nullptr;
    std::string name;
    if (x->kind == NK::Ident) {
      name = static_cast<Ident*>(x)->name;
      Found f = find(name, x->loc);
      if (f.via || f.enum_type) fail(x->loc, "#location: '", name, "' (brought in by 'using') is not a declaration");
      sym = f.sym ? f.sym : lookup(name, x->loc);
    } else if (x->kind == NK::Member && static_cast<Member*>(x)->base) {
      auto* m = static_cast<Member*>(x);
      if (Scope* ms = module_of(m->base)) {
        name = m->name;
        sym = module_member(ms, m);
      }
    }
    // An expression: where it starts (a Binary's own location is its operator).
    Expr* first = x;
    for (;;) {
      if (first->kind == NK::Binary) first = static_cast<Binary*>(first)->lhs;
      else if (first->kind == NK::Index) first = static_cast<Index*>(first)->base;
      else if (first->kind == NK::Call) first = static_cast<Call*>(first)->callee;
      else if (first->kind == NK::Member && static_cast<Member*>(first)->base) first = static_cast<Member*>(first)->base;
      else break;
    }
    SourceLoc at = first->loc;
    if (sym) {
      if (sym->loc.line == 0) fail(x->loc, "#location: '", name, "' is built in and has no source location");
      at = sym->loc;
    }
    d->type = source_loc_t_;
    d->cv = location_value(at);
    return d->type;
  }

  // ------------------------------------------------------------------ setup

  void add_type(Scope* sc, const char* name, Type* t) {
    Symbol* s = declare(sc, name, SymKind::Type, SourceLoc{});
    s->type = T.type_t;
    s->cv = ConstValue::of_type(t);
    s->state = Symbol::State::Resolved;
  }

  void add_builtin(Scope* sc, const char* name, Builtin b) {
    if (sc->syms.count(name)) return;
    Symbol* s = declare(sc, name, SymKind::Builtin, SourceLoc{});
    s->builtin = b;
    s->state = Symbol::State::Resolved;
  }

  void add_const(Scope* sc, const char* name, Type* t, ConstValue cv) {
    if (sc->syms.count(name)) return;
    Symbol* s = declare(sc, name, SymKind::Const, SourceLoc{});
    s->type = t;
    s->cv = std::move(cv);
    s->state = Symbol::State::Resolved;
  }

  void setup_universe() {
    add_type(universe_, "void", T.void_t);
    add_type(universe_, "bool", T.bool_t);
    add_type(universe_, "s8", T.s8);
    add_type(universe_, "s16", T.s16);
    add_type(universe_, "s32", T.s32);
    add_type(universe_, "s64", T.s64);
    add_type(universe_, "u8", T.u8);
    add_type(universe_, "u16", T.u16);
    add_type(universe_, "u32", T.u32);
    add_type(universe_, "u64", T.u64);
    add_type(universe_, "int", T.s64);
    add_type(universe_, "float32", T.f32);
    add_type(universe_, "float64", T.f64);
    add_type(universe_, "float", T.f32);
    add_type(universe_, "string", T.string_t);
    add_type(universe_, "Any", T.any_t);
    add_type(universe_, "Type", T.type_t);
    add_type(universe_, "Code", T.code_t);
    add_builtin(universe_, "size_of", Builtin::SizeOf);
    add_builtin(universe_, "type_of", Builtin::TypeOf);
    add_builtin(universe_, "type_info", Builtin::TypeInfo);
  }

  // ------------------------------------------------------------------ the context (Preload)

  // Parses the Preload into its own scope. Its declarations are only resolved
  // when something uses them; its procedures can use Basic's built-ins and the
  // runtime's raw allocator, which the program does not see.
  void setup_preload() {
    Scope* internals = new_scope(universe_, nullptr);
    internals->is_module = true;
    add_module_builtins(internals, "Basic");
    add_builtin(internals, "__ojay_raw_alloc", Builtin::RawAlloc);
    add_builtin(internals, "__ojay_raw_free", Builtin::RawFree);
    add_builtin(internals, "__ojay_raw_realloc", Builtin::RawRealloc);
    add_builtin(internals, "__ojay_raw_alloc_aligned", Builtin::RawAllocAligned);
    preload_ = new_scope(universe_, nullptr);
    preload_->is_module = true;
    preload_->imports.push_back(internals);
    preload_->imports.push_back(core_);
    std::vector<Stmt*> stmts = parse_source_stmts(ast_, sm_, sm_.add("<preload>", kPreloadSource), true);
    Scope* saved = scope_;
    scope_ = preload_;
    lazy_collect_ = true;
    collect(stmts, true);
    lazy_collect_ = false;
    scope_ = saved;
    context_sym_ = declare(preload_, "context", SymKind::Var, SourceLoc{});
    context_sym_->is_context = true;
    context_sym_->is_global = true;
    context_sym_->state = Symbol::State::Resolved;
  }

  // The part of built-in module `module` that is written in Jai, collected
  // like the Preload: its declarations are only resolved when used. It sees
  // the module's built-ins and the Preload.
  void add_module_source(Scope* sc, const std::string& module) {
    const char* src = module == "Basic" ? kBasicSource : module == "Math" ? kMathSource : nullptr;
    if (!src) return;
    std::vector<Stmt*> stmts = parse_source_stmts(ast_, sm_, sm_.add("<" + module + ">", src), true);
    Scope* saved = scope_;
    scope_ = sc;
    lazy_collect_ = true;
    collect(stmts, true);
    lazy_collect_ = false;
    scope_ = saved;
    sc->imports.push_back(preload_);
    sc->imports.push_back(core_);
  }

  Symbol* preload_sym(const char* name) { return preload_->syms.at(name); }

  // ------------------------------------------------------------------ runtime type information

  // Type_Info and the Type_Info_* structs, visible in every module. They are
  // resolved up front (they are only types: they cost nothing until used),
  // as codegen and the #run interpreter lay out every type's Type_Info with them.
  void setup_type_info() {
    core_ = new_scope(universe_, nullptr);
    core_->is_module = true;
    std::vector<Stmt*> stmts = parse_source_stmts(ast_, sm_, sm_.add("<core>", kCoreSource), true);
    Scope* saved = scope_;
    scope_ = core_;
    lazy_collect_ = true;
    collect(stmts, true);
    lazy_collect_ = false;
    auto get = [&](const char* name) {
      Symbol* sym = core_->syms.at(name);
      resolve(sym);
      Type* t = sym->cv.t;
      complete_type(t, SourceLoc{});
      return t;
    };
    auto& ti = prog_.type_info;
    ti.info = get("Type_Info");
    ti.integer = get("Type_Info_Integer");
    ti.float_ = get("Type_Info_Float");
    ti.pointer = get("Type_Info_Pointer");
    ti.array = get("Type_Info_Array");
    ti.struct_ = get("Type_Info_Struct");
    ti.member = get("Type_Info_Struct_Member");
    ti.enum_ = get("Type_Info_Enum");
    ti.procedure = get("Type_Info_Procedure");
    ti.variant = get("Type_Info_Variant");
    for_flags_t_ = get("For_Flags");
    source_loc_t_ = get("Source_Code_Location");
    scope_ = saved;
  }

  // Whether t is Type_Info or one of the Type_Info_* structs that start with one.
  bool is_type_info_struct(const Type* t) const {
    const auto& ti = prog_.type_info;
    return t == ti.info || t == ti.integer || t == ti.float_ || t == ti.pointer || t == ti.array ||
           t == ti.struct_ || t == ti.enum_ || t == ti.procedure || t == ti.variant;
  }
  bool is_type_info_ptr(const Type* t) const { return t->is_pointer() && is_type_info_struct(t->elem); }

  // The Type_Info_* struct type_info(t) returns for t.
  Type* type_info_struct_for(Type* t) const {
    const auto& ti = prog_.type_info;
    if (t->distinct_of) return ti.variant;
    switch (t->kind) {
      case TypeKind::Int: return ti.integer;
      case TypeKind::Float: return ti.float_;
      case TypeKind::Pointer: case TypeKind::Null: return ti.pointer;
      case TypeKind::Array: return ti.array;
      case TypeKind::Struct: return ti.struct_;
      case TypeKind::Enum: return ti.enum_;
      case TypeKind::Proc: return ti.procedure;
      default: return ti.info;
    }
  }

  // The program uses the context (or something else of the Preload): from now
  // on allocations go through context.allocator, and code generation needs
  // the default context and __allocate.
  void ensure_context_support() {
    if (context_ready_ || context_resolving_) return;
    context_resolving_ = true;
    prog_.uses_context = true;
    Symbol* ctx = preload_sym("Context");
    resolve(ctx);
    context_type_ = ctx->cv.t;
    context_sym_->type = context_type_;
    Symbol* al = preload_sym("Allocator");
    resolve(al);
    prog_.allocator_type = al->cv.t;
    complete_type(context_type_, SourceLoc{});
    Symbol* dc = preload_sym("__default_context");
    resolve(dc);
    prog_.default_context = dc;
    Symbol* ap = preload_sym("__allocate");
    resolve(ap);
    prog_.allocate_proc = ap->proc;
    context_resolving_ = false;
    context_ready_ = true;
  }

  // New / NewArray of t, whose alignment (#align) may be more than the heap
  // guarantees: allocations go through __allocate_aligned.
  void want_aligned_allocation(Type* t) {
    if (t->align <= T.heap_align() || prog_.allocate_aligned_proc) return;
    Symbol* s = preload_sym("__allocate_aligned");
    resolve(s);
    prog_.allocate_aligned_proc = s->proc;
  }

  Type* allocator_type() {
    ensure_context_support();
    return prog_.allocator_type;
  }

  // Whether the code being checked has a context: everything but #c_call and
  // #no_context procedures, unless inside a push_context.
  bool has_context() const { return !proc_ || !(proc_->c_call || proc_->no_context) || push_depth_ > 0; }

  static std::string proc_desc(const ProcLit* p) { return p->name.empty() ? "<anonymous>" : "'" + p->name + "'"; }
  const char* no_context_why() const { return proc_->c_call ? "#c_call" : "#no_context"; }

  // `what` needs a context.
  void require_context(SourceLoc loc, const std::string& what) {
    if (has_context()) return;
    fail(loc, what, " needs a context, but ", no_context_why(), " procedure ", proc_desc(proc_),
         " has none (give it one with push_context)");
  }

  // `context`
  Type* check_context_ident(Ident* id) {
    ensure_context_support();
    if (!proc_ && !in_run_operand_) fail(id->loc, "'context' only exists inside procedures");
    if (!has_context())
      fail(id->loc, "cannot use 'context' in ", no_context_why(), " procedure ", proc_desc(proc_),
           ": it has no context (give it one with push_context)");
    complete_type(context_type_, id->loc);
    return context_type_;
  }

  void check_push_context(PushContext* pc) {
    ensure_context_support();
    if (!proc_) fail(pc->loc, "push_context is only allowed inside a procedure");
    Type* t = check_rv(pc->ctx, context_type_);
    if (t != context_type_)
      fail(pc->ctx->loc, "push_context needs a value of type Context, not ", type_name(t));
    ++push_depth_;
    check_block(pc->body);
    --push_depth_;
  }

  // The allocator argument of an allocating built-in.
  void set_allocator_arg(Call* c, Expr* a) {
    if (!a) return;
    check_value(a, allocator_type());
    c->allocator = a;
  }

  static bool is_basic_name(const std::string& n) {
    static const std::unordered_set<std::string> names = {
        "print", "sprint", "tprint", "write_string", "alloc", "free", "realloc", "New", "NewArray",
        "array_add", "array_free", "array_reset", "assert", "exit", "memcpy", "memset", "min", "max",
        "to_c_string", "get_command_line_arguments", "array_reserve", "array_resize", "array_copy", "array_find",
        "array_add_if_unique", "array_insert_at", "array_ordered_remove_by_index", "array_unordered_remove_by_index",
        "array_ordered_remove_by_value", "array_unordered_remove_by_value", "array_view", "peek", "peek_pointer",
        "pop", "swap", "c_style_strlen", "to_string", "alloc_string", "copy_string", "compare", "is_digit",
        "is_alpha", "is_alnum", "is_space", "String_Builder", "init_string_builder", "append", "print_to_builder",
        "builder_string_length", "builder_to_string", "free_buffers", "log_error"};
    return names.count(n) > 0;
  }
  static bool is_math_name(const std::string& n) {
    static const std::unordered_set<std::string> names = {
        "sqrt", "sin", "cos", "tan", "atan2", "pow", "floor", "ceil", "round", "exp", "log", "abs",
        "PI", "TAU", "S8_MIN", "S8_MAX", "S16_MIN", "S16_MAX", "S32_MIN", "S32_MAX", "S64_MIN",
        "S64_MAX", "U8_MAX", "U16_MAX", "U32_MAX", "U64_MAX", "FLOAT32_MAX", "FLOAT64_MAX", "PI64", "TAU64",
        "clamp", "lerp", "Vector2", "Vector3", "Vector4", "make_vector2", "make_vector3", "make_vector4", "dot",
        "cross", "length", "length_squared", "distance", "normalize"};
    return names.count(n) > 0;
  }

  void add_module_builtins(Scope* sc, const std::string& module) {
    auto limits = [&] {
      add_const(sc, "S8_MIN", T.untyped_int, ConstValue::of_int(INT8_MIN));
      add_const(sc, "S8_MAX", T.untyped_int, ConstValue::of_int(INT8_MAX));
      add_const(sc, "S16_MIN", T.untyped_int, ConstValue::of_int(INT16_MIN));
      add_const(sc, "S16_MAX", T.untyped_int, ConstValue::of_int(INT16_MAX));
      add_const(sc, "S32_MIN", T.untyped_int, ConstValue::of_int(INT32_MIN));
      add_const(sc, "S32_MAX", T.untyped_int, ConstValue::of_int(INT32_MAX));
      add_const(sc, "S64_MIN", T.untyped_int, ConstValue::of_int(INT64_MIN));
      add_const(sc, "S64_MAX", T.untyped_int, ConstValue::of_int(INT64_MAX));
      add_const(sc, "U8_MAX", T.untyped_int, ConstValue::of_int(UINT8_MAX));
      add_const(sc, "U16_MAX", T.untyped_int, ConstValue::of_int(UINT16_MAX));
      add_const(sc, "U32_MAX", T.untyped_int, ConstValue::of_int(UINT32_MAX));
      add_const(sc, "U64_MAX", T.untyped_int, ConstValue::of_int((i128)UINT64_MAX));
    };
    if (module == "Basic") {
      add_builtin(sc, "print", Builtin::Print);
      add_builtin(sc, "sprint", Builtin::Sprint);
      add_builtin(sc, "tprint", Builtin::Sprint);
      add_builtin(sc, "write_string", Builtin::WriteString);
      add_builtin(sc, "alloc", Builtin::Alloc);
      add_builtin(sc, "free", Builtin::Free);
      add_builtin(sc, "realloc", Builtin::Realloc);
      add_builtin(sc, "New", Builtin::New);
      add_builtin(sc, "NewArray", Builtin::NewArray);
      add_builtin(sc, "array_add", Builtin::ArrayAdd);
      add_builtin(sc, "array_reserve", Builtin::ArrayReserve);
      add_builtin(sc, "array_free", Builtin::ArrayFree);
      add_builtin(sc, "array_reset", Builtin::ArrayReset);
      add_builtin(sc, "assert", Builtin::Assert);
      add_builtin(sc, "exit", Builtin::Exit);
      add_builtin(sc, "memcpy", Builtin::Memcpy);
      add_builtin(sc, "memset", Builtin::Memset);
      add_builtin(sc, "min", Builtin::Min);
      add_builtin(sc, "max", Builtin::Max);
      add_builtin(sc, "to_c_string", Builtin::ToCString);
      add_builtin(sc, "get_command_line_arguments", Builtin::CommandLineArguments);
      limits();
    } else if (module == "Hardware") {
      add_builtin(sc, "volatile_load", Builtin::VolatileLoad);
      add_builtin(sc, "volatile_store", Builtin::VolatileStore);
    } else if (module == "Math") {
      add_builtin(sc, "sqrt", Builtin::Sqrt);
      add_builtin(sc, "sin", Builtin::Sin);
      add_builtin(sc, "cos", Builtin::Cos);
      add_builtin(sc, "tan", Builtin::Tan);
      add_builtin(sc, "atan2", Builtin::Atan2);
      add_builtin(sc, "pow", Builtin::Pow);
      add_builtin(sc, "floor", Builtin::Floor);
      add_builtin(sc, "ceil", Builtin::Ceil);
      add_builtin(sc, "round", Builtin::Round);
      add_builtin(sc, "exp", Builtin::Exp);
      add_builtin(sc, "log", Builtin::Log);
      add_builtin(sc, "abs", Builtin::Abs);
      add_builtin(sc, "min", Builtin::Min);
      add_builtin(sc, "max", Builtin::Max);
      add_const(sc, "PI", T.untyped_float, ConstValue::of_float(3.14159265358979323846));
      add_const(sc, "TAU", T.untyped_float, ConstValue::of_float(6.28318530717958647692));
      add_const(sc, "FLOAT32_MAX", T.untyped_float, ConstValue::of_float(3.40282346638528859812e+38));
      add_const(sc, "FLOAT64_MAX", T.untyped_float, ConstValue::of_float(1.79769313486231570815e+308));
      limits();
    }
  }

  // ------------------------------------------------------------------ collection

  // The scope of a file's top level (`#scope_file` declarations), inside the
  // current unit's scope. Every top-level declaration is resolved in its
  // file's scope, so it sees the file's private declarations; the others are
  // in the unit's scope. The Preload and the built-in modules have none.
  Scope* file_scope_for(uint32_t file) {
    if (lazy_collect_) return scope_;
    Scope*& fs = file_scopes_[{scope_, file}];
    if (!fs) {
      fs = new_scope(scope_, nullptr);
      fs->is_module = true;
    }
    return fs;
  }

  static void set_vis(Stmt* s, ScopeVis vis) {
    s->vis = vis;
    if (s->kind == NK::StaticIf) {
      auto* si = static_cast<StaticIf*>(s);
      for (Stmt* x : si->then_s) set_vis(x, vis);
      for (Stmt* x : si->else_s) set_vis(x, vis);
    }
  }

  // Whether a declaration of an imported module is visible to the importer
  // (not #scope_module or #scope_file).
  static bool exported(const Symbol* s) { return !s->decl || s->decl->vis == ScopeVis::Export; }

  // Symbol `s` (from an imported module's table), or the first exported
  // procedure of its overload set when `s` itself is not exported.
  static Symbol* exported_sym(Symbol* s) {
    if (exported(s)) return s;
    for (Symbol* o : s->overloads)
      if (exported(o)) return o;
    return nullptr;
  }

  // `nested`: statements of a taken #if branch or a top-level #insert, which
  // belong to the file of the statement that made them (cur_file_scope_).
  void collect(const std::vector<Stmt*>& stmts, bool nested) {
    std::vector<std::pair<StaticIf*, Scope*>> static_ifs;
    std::vector<std::pair<Insert*, Scope*>> inserts;
    for (Stmt* s : stmts) {
      Scope* fs = lazy_collect_ ? scope_ : nested ? cur_file_scope_ : file_scope_for(s->loc.file);
      switch (s->kind) {
        case NK::Decl: {
          auto* d = static_cast<Decl*>(s);
          d->is_global = true;
          if (d->add_context) {  // a member of Context, added when Context is laid out
            if (context_type_ && context_type_->layout_state != Type::LayoutState::None)
              fail(d->loc, "#add_context after Context has been used (by #run code that ran earlier)");
            context_additions_.push_back({d, fs});
            break;
          }
          Scope* into = d->vis == ScopeVis::File ? fs : scope_;
          for (size_t i = 0; i < d->names.size(); ++i) {
            Symbol* sym = d->is_const ? declare_const(into, d, i)
                                      : declare(into, d->names[i].name, SymKind::Var, d->names[i].loc);
            sym->scope = fs;  // resolved in its file's scope
            sym->decl = d;
            sym->decl_index = (int)i;
            sym->is_global = true;
            d->syms.push_back(sym);
            if (!lazy_collect_) global_order_.push_back(sym);
          }
          break;
        }
        case NK::Import: {
          Scope* m = prog_.modules.at(static_cast<Import*>(s)->module_id).scope;
          if (m != scope_ && std::find(scope_->imports.begin(), scope_->imports.end(), m) == scope_->imports.end())
            scope_->imports.push_back(m);
          break;
        }
        case NK::Load:
          break;  // handled by the driver
        case NK::StaticIf:
          static_ifs.push_back({static_cast<StaticIf*>(s), fs});
          break;
        case NK::Insert:
          inserts.push_back({static_cast<Insert*>(s), fs});
          break;
        case NK::ExprStmt:
          if (static_cast<ExprStmt*>(s)->expr->kind == NK::Directive) {
            top_directives_.push_back({s, fs});
            break;
          }
          fail(s->loc, "only declarations are allowed at top level");
        default:
          fail(s->loc, "only declarations are allowed at top level");
      }
    }
    Scope* unit = scope_;
    Scope* saved_fs = cur_file_scope_;
    for (auto [si, fs] : static_ifs) {
      scope_ = fs;
      bool taken = eval_static_cond(si->cond);
      scope_ = unit;
      si->taken = taken ? 1 : 0;
      cur_file_scope_ = fs;
      collect(taken ? si->then_s : si->else_s, true);
      cur_file_scope_ = saved_fs;
    }
    // Top-level #insert: declarations made at compile time. They are
    // evaluated once the unit's own declarations are known.
    for (auto [ins, fs] : inserts) {
      scope_ = fs;
      ConstValue v = insert_operand(ins->operand);
      scope_ = unit;
      auto* blk = ast_.make<Block>(ins->loc);
      ins->result = blk;
      with_note(insert_note(ins->loc), [&] {
        if (v.kind == ConstValue::String) {
          blk->stmts = parse_source_stmts(ast_, sm_, insert_source(v.s, ins->loc), true);
        } else {
          const CodeEntry& c = code_entry(v);
          if (!c.block) fail(c.loc, "only a #code block of declarations can be inserted at top level");
          blk->stmts = static_cast<Block*>(clone_stmt(ast_, c.block))->stmts;
        }
        for (Stmt* x : blk->stmts) {
          if (x->kind == NK::Decl) static_cast<Decl*>(x)->is_global = true;
          set_vis(x, ins->vis);  // the #scope_* of the #insert
        }
        cur_file_scope_ = fs;
        collect(blk->stmts, true);
        cur_file_scope_ = saved_fs;
        return 0;
      });
    }
  }

  // A #if condition: a constant bool, or a number or enum (`#if flags & .REVERSE`) that is not zero.
  bool eval_static_cond(Expr*& cond) {
    Type* t = check_value(cond, nullptr);
    if (t->is_untyped() || t->kind == TypeKind::Int || t->kind == TypeKind::Enum) {
      if (cond->is_const() && cond->cv.kind == ConstValue::Int) return cond->cv.i != 0;
    } else {
      coerce(cond, T.bool_t);
    }
    if (!cond->is_const() || cond->cv.kind != ConstValue::Bool)
      fail(cond->loc, "#if condition must be a compile-time constant (a bool, number or enum)");
    return cond->cv.b;
  }

  // `#assert cond;` and `#run expr;` statements (at top level or in a body).
  void check_directive_stmt(Stmt* s) {
    auto* d = static_cast<DirectiveExpr*>(static_cast<ExprStmt*>(s)->expr);
    if (d->name == "run") {
      check_run(d, nullptr, /*statement=*/true);
      return;
    }
    if (d->name != "assert") fail(d->loc, "#", d->name, " is not supported as a statement yet");
    check_value(d->operand, T.bool_t);
    if (!d->operand->is_const()) fail(d->loc, "#assert condition must be a compile-time constant");
    if (!d->operand->cv.b) fail(d->loc, "#assert failed");
  }

  // `#run expr`: checks `expr` for compile-time execution and evaluates it.
  // As an expression, the result becomes the constant value of the #run; as
  // a statement, it is run for its side effects and its value is discarded.
  Type* check_run(DirectiveExpr* d, Type* hint, bool statement) {
    Type* t;
    {
      ComptimeMode mode(*this, true, nullptr, true);
      if (statement) {
        t = check(d->operand, nullptr);
        if (t->is_untyped()) default_untyped(d->operand);
      } else {
        t = check_rv(d->operand, hint);
      }
    }
    if (statement) {
      if (!d->operand->is_const()) interp_.run(d->operand, d->loc, false);
      d->type = T.void_t;
      return T.void_t;
    }
    if (d->operand->is_const()) {  // nothing to execute: `#run 1 + 2`
      d->cv = d->operand->cv;
      return t;
    }
    complete_type(t, d->loc);
    d->cv = interp_.run(d->operand, d->loc, true);
    if (d->cv.kind == ConstValue::None) fail(d->loc, "#run expression does not produce a value");
    return t;
  }

  // ------------------------------------------------------------------ symbol resolution

  void resolve(Symbol* s) {
    // Anything from the Preload brings in the context machinery.
    if (s->scope == preload_ && !context_ready_) ensure_context_support();
    if (s->state == Symbol::State::Resolved) return;
    if (s->state == Symbol::State::Resolving)
      fail(s->loc, "circular dependency: '", s->name, "' depends on itself");
    s->state = Symbol::State::Resolving;
    // A global declaration is the same wherever it is first used from: its
    // procedures are registered even if a #run triggered its resolution.
    std::optional<ComptimeMode> mode;
    if (s->is_global) mode.emplace(*this, false, nullptr, false);
    Context c = save();
    scope_ = s->scope;
    proc_ = s->scope->proc;
    loops_.clear();
    defer_depth_ = 0;
    if (s->is_global) macros_.clear();
    try {
      if (s->decl->is_const) resolve_const(s);
      else resolve_global_var(s);
    } catch (...) {
      // Overload resolution may catch the error (a polymorphic candidate that
      // does not fit); the symbol must then be resolvable again.
      restore(c);
      s->state = Symbol::State::Unresolved;
      throw;
    }
    restore(c);
    s->state = Symbol::State::Resolved;
  }

  void resolve_const(Symbol* s) {
    Decl* d = s->decl;
    if (d->values.size() != d->names.size())
      fail(d->loc, "constant declaration needs one value per name");
    Expr*& v = d->values[s->decl_index];
    Type* declared = d->type_expr ? resolve_type(d->type_expr) : nullptr;
    switch (v->kind) {
      case NK::ProcLit: {
        auto* p = static_cast<ProcLit*>(v);
        if (p->name.empty()) p->name = s->name;
        if (p->is_poly || p->poly_returns) {
          validate_poly_proc(p);
          s->kind = SymKind::PolyProc;
          s->proc = p;
          return;
        }
        s->kind = SymKind::Proc;
        s->proc = p;
        define_proc(p);
        s->type = p->type;
        v->type = p->type;
        return;
      }
      case NK::StructDef: {
        auto* sd = static_cast<StructDef*>(v);
        if (!sd->params.empty()) {
          validate_poly_struct(sd, s->name);
          s->kind = SymKind::PolyStruct;
          return;
        }
        s->kind = SymKind::Type;
        s->type = T.type_t;
        s->cv = ConstValue::of_type(define_struct(sd, s->name));
        v->type = T.type_t;
        v->cv = s->cv;
        return;
      }
      case NK::EnumDef: {
        auto* ed = static_cast<EnumDef*>(v);
        s->kind = SymKind::Type;
        s->type = T.type_t;
        s->cv = ConstValue::of_type(define_enum(ed, s->name));
        v->type = T.type_t;
        v->cv = s->cv;
        return;
      }
      case NK::Library: {
        auto* lib = static_cast<LibraryExpr*>(v);
        s->kind = SymKind::Library;
        s->library = lib;
        if (lib->system && libs_seen_.insert(lib->lib_name).second) prog_.system_libraries.push_back(lib->lib_name);
        return;
      }
      case NK::Directive: {
        auto* de = static_cast<DirectiveExpr*>(v);
        if (de->name.rfind("import:", 0) == 0) {
          s->kind = SymKind::Module;
          s->module_scope = prog_.modules.at(de->module_id).scope;
          return;
        }
        if (de->name == "distinct" || de->name == "isa") {
          s->kind = SymKind::Type;
          s->type = T.type_t;
          s->cv = ConstValue::of_type(define_distinct(de, s->name));
          v->type = T.type_t;
          v->cv = s->cv;
          return;
        }
        break;
      }
      default:
        break;
    }
    Type* t = check_rv(v, declared);
    if (declared) {
      coerce(v, declared);
      t = declared;
    }
    if (!v->is_const()) fail(v->loc, "value of constant '", s->name, "' is not a compile-time constant");
    if (v->cv.kind == ConstValue::TypeV) {
      s->kind = SymKind::Type;
      s->type = T.type_t;
      s->cv = v->cv;
      return;
    }
    s->kind = SymKind::Const;
    s->type = t;
    s->cv = v->cv;
  }

  void resolve_global_var(Symbol* s) {
    Decl* d = s->decl;
    Type* declared = nullptr;
    if (d->type_expr) declared = d->declared_type ? d->declared_type : resolve_type(d->type_expr);
    d->declared_type = declared;
    Expr* init = nullptr;
    if (!d->values.empty()) {
      if (d->values.size() != d->names.size()) fail(d->loc, "global declaration needs one value per name");
      Expr*& v = d->values[s->decl_index];
      check_value(v, declared);
      if (!is_static_init(v)) fail(v->loc, "global variable initializer must be a compile-time constant");
      init = v;
      if (!declared) declared = v->type;
    }
    if (!declared) fail(s->loc, "global variable '", s->name, "' needs a type or a value");
    if (declared->kind == TypeKind::Void) fail(s->loc, "variable cannot have type void");
    complete_type(declared, s->loc);
    s->kind = SymKind::Var;
    s->type = declared;
    s->align = align_value(d->align);
    s->init = init;
    s->is_global = true;
    prog_.global_vars.push_back(s);
  }

  bool is_static_init(Expr* e) {
    if (e->is_const()) return true;
    switch (e->kind) {
      case NK::StructLit:
        for (Expr* f : static_cast<StructLit*>(e)->field_values)
          if (f && !is_static_init(f)) return false;
        return true;
      case NK::ArrayLit:
        for (Expr* x : static_cast<ArrayLit*>(e)->elems)
          if (!is_static_init(x)) return false;
        return true;
      case NK::ProcLit:
        return true;
      case NK::Ident:
        return static_cast<Ident*>(e)->sym->kind == SymKind::Proc;
      case NK::Unary: {  // the address of a global variable
        auto* u = static_cast<Unary*>(e);
        if (u->op != UnOp::AddrOf || u->operand->kind != NK::Ident) return false;
        Symbol* g = static_cast<Ident*>(u->operand)->sym;
        return g->kind == SymKind::Var && g->is_global && !g->is_context;
      }
      case NK::ImplicitConv: {
        auto* ic = static_cast<ImplicitConv*>(e);
        return ic->type->kind != TypeKind::Any && ic->operand->type->kind != TypeKind::Array &&
               is_static_init(ic->operand);
      }
      default:
        return false;
    }
  }

  // ------------------------------------------------------------------ definitions

  Type* define_struct(StructDef* sd, const std::string& name) {
    if (sd->struct_type) return sd->struct_type;
    Type* t = T.new_struct(name.empty() ? (sd->is_union ? "union" : "struct") : name);
    t->is_union = sd->is_union;
    sd->struct_type = t;
    // The body's own scope holds its constants; #this names the struct there.
    Scope* ss = new_scope(scope_, scope_->proc);
    ss->this_type = t;
    struct_defs_[t] = {sd, ss};
    all_structs_.push_back(t);
    for (Decl* d : sd->members)
      if (d->is_const) declare_struct_constant(ss, d);
    return t;
  }

  // Declares the names of constant member `d` (`N :: 3;`) in struct scope ss;
  // they are resolved when used, and all of them once the struct is laid out.
  void declare_struct_constant(Scope* ss, Decl* d) {
    if (d->values.size() != d->names.size()) fail(d->loc, "constant declaration needs one value per name");
    for (size_t i = 0; i < d->names.size(); ++i) {
      Symbol* sym = declare_const(ss, d, i);
      sym->decl = d;
      sym->decl_index = (int)i;
      sym->is_global = ss->proc == nullptr;
      d->syms.push_back(sym);
    }
  }

  // The constant `name` of struct t (or of the struct a distinct type copies), or null.
  Symbol* struct_constant(Type* t, const std::string& name, SourceLoc loc) {
    while (t->distinct_of) t = t->distinct_of;
    auto it = struct_defs_.find(t);
    if (it == struct_defs_.end()) return nullptr;
    Scope* ss = it->second.second;
    auto jt = ss->syms.find(name);
    if (jt == ss->syms.end() && t->layout_state == Type::LayoutState::None) {
      complete_type(t, loc);  // constants made by an #insert in the body
      jt = ss->syms.find(name);
    }
    return jt == ss->syms.end() ? nullptr : jt->second;
  }

  // `S.N` or `value.N` (also through a pointer): the struct constant it names, if any.
  Symbol* member_constant(Member* m) {
    Type* bt = check_rv(m->base, nullptr);
    Type* st = nullptr;
    bool type_base = m->base->is_const() && m->base->cv.kind == ConstValue::TypeV;
    if (type_base) st = m->base->cv.t;
    else st = bt->is_pointer() ? bt->elem : bt;
    if (!st || st->kind != TypeKind::Struct) return nullptr;
    if (!type_base) {
      complete_type(st, m->loc);
      std::vector<int> path;
      if (find_field(st, m->name, path)) return nullptr;  // a field
    }
    return struct_constant(st, m->name, m->loc);
  }

  // `name :: #type,distinct Base;`
  Type* define_distinct(DirectiveExpr* de, const std::string& name) {
    Type* base = resolve_type(de->operand);
    switch (base->kind) {
      case TypeKind::Void: case TypeKind::Tuple: case TypeKind::Code: case TypeKind::UntypedInt:
      case TypeKind::UntypedFloat: case TypeKind::Null:
        fail(de->loc, "a distinct type of ", type_name(base), " is not supported");
      default:
        break;
    }
    Type* t = T.new_distinct(name, base, de->name == "isa");
    if (t->kind == TypeKind::Struct) all_structs_.push_back(t);  // laid out as its base is
    return t;
  }

  Type* define_enum(EnumDef* ed, const std::string& name) {
    if (ed->enum_type) return ed->enum_type;
    Type* base = T.s64;
    if (ed->base_type) {
      base = resolve_type(ed->base_type);
      if (base->kind != TypeKind::Int) fail(ed->base_type->loc, "enum base type must be an integer type");
    }
    Type* t = T.new_enum(name.empty() ? "enum" : name, base, ed->is_flags);
    ed->enum_type = t;
    i128 next = ed->is_flags ? 1 : 0;
    std::unordered_set<std::string> seen;
    for (auto& m : ed->members) {
      if (!seen.insert(m.name).second) fail(m.loc, "duplicate enum member '", m.name, "'");
      i128 val = next;
      if (m.value) {
        check_value(m.value, base);
        if (!m.value->is_const() || m.value->cv.kind != ConstValue::Int)
          fail(m.value->loc, "enum value must be a compile-time integer constant");
        val = m.value->cv.i;
      }
      if (!int_fits(val, base)) fail(m.loc, "enum value ", i128_str(val), " does not fit in ", type_name(base));
      t->members.push_back({m.name, val});
      if (ed->is_flags) {
        next = 1;
        while (next <= val && next > 0) next <<= 1;
      } else {
        next = val + 1;
      }
    }
    return t;
  }

  // Checks a procedure's signature. Procedures are registered for code
  // generation and body checking right away, except polymorphic instances,
  // which are registered once a call actually uses them (use_instance).
  void define_proc(ProcLit* p, bool for_codegen = true) {
    // The procedure type a short lambda is used as (check of a ProcLit): it
    // gives untyped parameters their types and, unless written, the results.
    Type* hint = lambda_hint_;
    bool hint_results = lambda_hint_results_;
    lambda_hint_ = nullptr;
    lambda_hint_results_ = true;
    if (p->signature_done) return;
    if (hint && (!p->short_expr || hint->params.size() != p->params.size())) hint = nullptr;
    if (p->modify && !poly_instances_.count(p))
      fail(p->modify->loc, "#modify is only allowed on polymorphic procedures");
    p->scope = scope_;
    p->enclosing = proc_;
    std::vector<Type*> params;
    std::unordered_set<std::string> names;
    for (size_t i = 0; i < p->params.size(); ++i) {
      Decl* d = p->params[i];
      if (!names.insert(d->names[0].name).second) fail(d->loc, "duplicate parameter '", d->names[0].name, "'");
      if (d->is_vararg && vararg_index(p) != (int)i) fail(d->loc, "a procedure can have only one variadic parameter");
      Type* t = nullptr;
      if (d->type_expr) {
        t = resolve_type(d->type_expr);
        if (d->is_vararg) t = T.array_of(t, ArrayKind::View);
      } else if (d->values.empty()) {  // a short lambda's `(x) => ...`
        if (!hint)
          fail(d->loc, "parameter '", d->names[0].name, "' needs a type: give it one ('", d->names[0].name,
               ": int'), or use the lambda where a procedure type is expected (a typed variable or parameter)");
        t = hint->params[i];
      }
      if (!d->values.empty()) {
        bool caller_loc = is_caller_location(d->values[0]);
        if (caller_loc && t && t != source_loc_t_)
          fail(d->values[0]->loc, "#caller_location is a Source_Code_Location, but parameter '", d->names[0].name,
               "' has type ", type_name(t));
        bool saved = in_param_default_;
        in_param_default_ = caller_loc;
        check_value(d->values[0], t);
        in_param_default_ = saved;
        if (!t) t = d->values[0]->type;
      }
      if (t->kind == TypeKind::Void) fail(d->loc, "parameter cannot have type void");
      d->declared_type = t;
      params.push_back(t);
    }
    std::vector<Type*> rets;
    for (auto& r : p->returns) {
      Type* t = resolve_type(r.type_expr);
      if (t->kind == TypeKind::Void) {
        if (p->returns.size() == 1) continue;  // `-> void` is the same as no return
        fail(r.loc, "void is not allowed in a return list");
      }
      rets.push_back(t);
    }
    if (rets.empty()) p->returns.clear();
    if (p->short_expr) {
      if (p->returns.empty()) {
        // Without `-> T`: the results of the procedure type it is used as (if
        // the parameters match), else the type of the expression.
        bool same = hint != nullptr && hint_results;
        for (size_t i = 0; same && i < params.size(); ++i) same = hint->params[i] == params[i];
        if (same) {
          rets = hint->returns;
          if (hint->c_call) p->c_call = true;
        } else {
          rets = short_lambda_results(p, params);
        }
      }
      if (!p->body) {
        p->body = ast_.make<Block>(p->short_expr->loc);
        if (rets.empty()) {
          p->body->stmts.push_back(ast_.make<ExprStmt>(p->short_expr->loc, p->short_expr));
        } else {
          auto* r = ast_.make<Return>(p->short_expr->loc);
          r->values.push_back(p->short_expr);
          p->body->stmts.push_back(r);
        }
      }
    }
    if (p->is_macro && rets.size() > 1)
      fail(p->returns.size() > 1 ? p->returns[1].loc : p->loc,
           "#expand macros with more than one return value are not supported yet");
    if (p->is_macro && p->c_call) fail(p->loc, "an #expand macro cannot be #c_call");
    p->type = T.proc(params, rets, p->c_call, vararg_index(p));
    if (p->is_foreign && p->variadic) fail(p->loc, "variadic #foreign procedures are not supported yet");
    if (p->program_export) {
      if (!p->c_call) fail(p->loc, "#program_export procedures must be #c_call (C calls them with the C calling convention)");
      if (p->is_foreign) fail(p->loc, "a #foreign procedure cannot be #program_export");
    }
    if (p->c_call) {
      for (size_t i = 0; i < params.size(); ++i)
        check_c_type(params[i], p->params[i]->loc, false);
      if (rets.size() > 1) fail(p->loc, "#c_call/#foreign procedures cannot return multiple values");
      if (rets.size() == 1) check_c_type(rets[0], p->returns.empty() ? p->loc : p->returns[0].loc, true);
    }
    if (p->is_foreign) {
      if (p->foreign_lib) {
        Expr* lib = p->foreign_lib;
        check(lib, nullptr);
        auto* id = static_cast<Ident*>(lib);
        if (!id->sym || id->sym->kind != SymKind::Library)
          fail(lib->loc, "'", id->name, "' is not a library (declare it with #system_library or #library)");
      }
      if (p->foreign_name.empty()) p->foreign_name = p->name;
      if (p->foreign_name.empty()) fail(p->loc, "anonymous #foreign procedures need an explicit symbol name");
    }
    p->signature_done = true;
    if (for_codegen) want_proc(p);
  }

  // The result types of short lambda `p` without `-> T`: the type of its
  // expression, checked (as a copy) with the parameters as locals; none if
  // the expression has no value, several for a call with several results.
  std::vector<Type*> short_lambda_results(ProcLit* p, const std::vector<Type*>& params) {
    ContextGuard guard(*this);  // restores the context however this ends
    in_run_operand_ = false;    // the parameters are the lambda's own, also in a #run expression
    proc_ = p;
    scope_ = new_scope(p->scope, p);
    loops_.clear();
    macros_.clear();
    for (size_t i = 0; i < p->params.size(); ++i) {
      Symbol* s = declare(scope_, p->params[i]->names[0].name, SymKind::Var, p->params[i]->names[0].loc);
      s->type = params[i];
      s->owner = p;
      s->state = Symbol::State::Resolved;
    }
    Expr* e = clone_expr(ast_, p->short_expr);
    std::vector<Type*> rets;
    Type* t = check(e, nullptr);
    if (t->kind == TypeKind::Tuple) {
      rets = t->elems;
    } else if (t->kind != TypeKind::Void) {
      default_untyped(e);
      rets.push_back(e->type);
    }
    return rets;
  }

  // Parameters and results of #c_call procedures must be C types (abi.h).
  void check_c_type(Type* t, SourceLoc loc, bool is_return) {
    // Its layout decides (#align / #place); codegen checks again the structs
    // that are still being laid out here (a member's procedure type).
    if (t->kind == TypeKind::Struct && t->layout_state == Type::LayoutState::None) complete_type(t, loc);
    std::string why = c_abi_unsupported(t);
    if (why.empty()) return;
    if (is_return) fail(loc, "#c_call/#foreign return type ", type_name(t), " is not supported: ", why);
    fail(loc, "#c_call/#foreign parameters of type ", type_name(t), " are not supported: ", why);
  }

  void complete_type(Type* t, SourceLoc loc) {
    switch (t->kind) {
      case TypeKind::Struct: {
        if (t->layout_state == Type::LayoutState::Done) return;
        if (t->distinct_of) {  // laid out as its base
          complete_type(t->distinct_of, loc);
          TypeTable::copy_distinct(t);
          return;
        }
        if (t->layout_state == Type::LayoutState::InProgress)
          fail(loc, "struct '", t->name, "' contains itself by value (use a pointer)");
        t->layout_state = Type::LayoutState::InProgress;
        if (poly_struct_types_.count(t)) {  // an instance of a polymorphic struct: say which one
          try {
            complete_struct(t);
          } catch (const CompileError& e) {
            throw CompileError(e.loc, cat(e.what(), "\n    in ", t->name, ", used at ", sm_.describe(loc)));
          }
        } else {
          complete_struct(t);
        }
        return;
      }
      case TypeKind::Array:
        if (t->array_kind == ArrayKind::Fixed) {
          if (t->distinct_of) complete_type(t->distinct_of, loc);
          complete_type(t->elem, loc);
          t->size = t->elem->size * t->count;
          t->align = t->elem->align;
        }
        return;
      case TypeKind::Tuple:
        for (Type* e : t->elems) complete_type(e, loc);
        return;
      default:
        return;
    }
  }

  // Resolves the members of struct `t` and lays it out.
  void complete_struct(Type* t) {
    auto [sd, sc] = struct_defs_.at(t);
    Context c = save();
    scope_ = sc;
    proc_ = sc->proc;
    macros_.clear();
    std::unordered_set<std::string> names;
    // A stack (next member last) of members and the scopes they are checked in.
    std::vector<std::pair<Decl*, Scope*>> todo;
    if (t == context_type_)  // #add_context members come after Context's own
      for (auto it = context_additions_.rbegin(); it != context_additions_.rend(); ++it) todo.push_back(*it);
    for (auto it = sd->members.rbegin(); it != sd->members.rend(); ++it) todo.push_back({*it, sc});
    struct ConstMember { Symbol* sym; size_t pos; bool listed; };  // pos: the number of fields before it
    std::vector<ConstMember> consts;
    int place = -1;  // a `#place m;` before the next member: m's field index
    while (!todo.empty()) {
      auto [d, dsc] = todo.back();
      todo.pop_back();
      scope_ = dsc;
      proc_ = dsc->proc;
      if (d->insert) {  // `#insert x;`: its members come next
        std::vector<Decl*> more = with_note(insert_note(d->loc), [&] { return inserted_members(d); });
        for (auto it = more.rbegin(); it != more.rend(); ++it) todo.push_back({*it, dsc});
        continue;
      }
      if (d->is_const) {  // a constant (declared with the struct, or made by an #insert just now)
        if (d->syms.empty()) declare_struct_constant(sc, d);
        for (size_t i = 0; i < d->syms.size(); ++i) {
          // A further procedure of an overload set is not listed again.
          bool further = sc->syms.at(d->names[i].name) != d->syms[i];
          if (!further && !names.insert(d->names[i].name).second)
            fail(d->names[i].loc, "duplicate member '", d->names[i].name, "'");
          consts.push_back({d->syms[i], t->fields.size(), !further});
        }
        continue;
      }
      if (!d->place_name.empty()) {  // `#place m;`
        if (t->is_union) fail(d->loc, "#place is not allowed in a union (its members all start at offset 0)");
        place = -1;
        for (size_t j = 0; j < t->fields.size(); ++j)
          if (!t->fields[j].anonymous && t->fields[j].name == d->place_name) place = (int)j;
        if (place < 0)
          fail(d->loc, "#place ", d->place_name, ": '", d->place_name, "' is not an earlier member of struct ",
               type_name(t));
        t->has_place = true;
        continue;
      }
      int64_t align = align_value(d->align);
      if (align) t->has_align = true;
      Type* ft = d->type_expr ? resolve_type(d->type_expr) : nullptr;
      if (ft && ft->kind == TypeKind::Void && !d->values.empty()) fail(d->loc, "a void member cannot have a value");
      if (!d->values.empty() && d->values.size() != 1 && d->values.size() != d->names.size())
        fail(d->loc, "member declaration needs one value per name");
      for (size_t i = 0; i < d->names.size(); ++i) {
        StructField f;
        f.name = d->names[i].name;
        f.anonymous = f.name.empty();
        f.is_using = d->is_using;
        if (!f.anonymous && !names.insert(f.name).second)
          fail(d->names[i].loc, "duplicate member '", f.name, "'");
        if (f.anonymous || f.is_using) {
          if (!ft || ft->kind != TypeKind::Struct) {
            if (ft && ft->is_pointer() && f.is_using)
              fail(d->loc, "'using' on a pointer member is not supported yet");
            fail(d->loc, "'using' needs a struct member");
          }
          complete_type(ft, d->loc);
          for (const std::string& n : member_names(ft))
            if (!names.insert(n).second) fail(d->loc, "duplicate member '", n, "'");
        }
        f.uninit = d->uninit;
        Type* mt = ft;
        if (!d->values.empty()) {
          Expr*& v = d->values[d->values.size() == 1 ? 0 : i];
          check_value(v, mt);
          if (!is_static_init(v)) fail(v->loc, "default member value must be a compile-time constant");
          f.default_value = v;
          if (!mt) mt = v->type;
        }
        if (!mt) fail(d->loc, "member '", f.name, "' needs a type");
        if (mt->kind == TypeKind::Void) {  // a marker (for #place): no storage
          if (t->is_union) fail(d->loc, "a union member cannot have type void");
          if (!d->values.empty()) fail(d->loc, "a void member cannot have a value");
        }
        complete_type(mt, d->loc);
        f.type = mt;
        f.align = align;
        if (i == 0) {
          f.place = place;
          place = -1;
        }
        t->fields.push_back(f);
      }
    }
    restore(c);
    T.layout_struct(t);
    if (t->has_place) check_overlapping_defaults(t);
    // Every constant, now that the struct's size is known (`N :: size_of(S);`).
    for (auto& [sym, pos, listed] : consts) {
      resolve(sym);
      if (!listed) continue;
      Type* ct = nullptr;
      switch (sym->kind) {
        case SymKind::Const:
          ct = sym->type->kind == TypeKind::UntypedInt ? T.s64 : sym->type->kind == TypeKind::UntypedFloat ? T.f32
               : sym->type->kind == TypeKind::Null ? T.void_ptr : sym->type;
          break;
        case SymKind::Type: ct = T.type_t; break;
        case SymKind::Proc: ct = sym->type; break;
        default: break;  // polymorphic procedures and structs, libraries, modules: not in Type_Info
      }
      if (ct) t->constants.push_back({sym->name, ct, pos});
    }
  }

  // The N of `#align N` (null: 0), a power-of-two constant.
  int64_t align_value(Expr*& e) {
    if (!e) return 0;
    check_value(e, nullptr);
    if (!e->is_const() || e->cv.kind != ConstValue::Int || !e->type->is_integer())
      fail(e->loc, "#align needs a compile-time integer constant");
    i128 n = e->cv.i;
    if (n <= 0 || (n & (n - 1)) != 0 || n > (i128(1) << 30))
      fail(e->loc, "#align ", i128_str(n), ": the alignment must be a power of two");
    return (int64_t)n;
  }

  // A value other than zero initializes member f (its default, or its type's).
  bool has_default(const StructField& f) {
    if (f.uninit) return false;
    return f.default_value || type_has_default(f.type);
  }
  bool type_has_default(Type* t) {
    if (t->is_fixed_array()) return t->count > 0 && type_has_default(t->elem);
    if (t->kind != TypeKind::Struct || t->is_union) return false;
    for (auto& f : t->fields)
      if (has_default(f)) return true;
    return false;
  }

  // Overlapping members (#place) are initialized by at most one default.
  void check_overlapping_defaults(Type* t) {
    for (size_t i = 0; i < t->fields.size(); ++i)
      for (size_t j = i + 1; j < t->fields.size(); ++j)
        if (fields_overlap(t, i, j) && has_default(t->fields[i]) && has_default(t->fields[j]))
          fail(t->fields[j].default_value ? t->fields[j].default_value->loc : struct_defs_.at(t).first->loc, "members '",
               t->fields[i].name, "' and '", t->fields[j].name, "' of ", type_name(t),
               " overlap (#place) and both have default values; at most one of them may");
  }

  // ------------------------------------------------------------------ procedure bodies

  void check_proc_body(ProcLit* p) {
    if (p->body_checked) return;
    p->body_checked = true;
    auto inst = poly_instances_.find(p);
    if (inst == poly_instances_.end()) return check_proc_body_inner(p);
    try {
      check_proc_body_inner(p);
    } catch (const CompileError& e) {
      throw CompileError(e.loc, cat(e.what(), "\n    in polymorphic procedure '", p->name, "' with ",
                                    inst->second.desc, ", first called at ", sm_.describe(inst->second.use)));
    }
  }

  void check_proc_body_inner(ProcLit* p) {
    Context c = save();
    proc_ = p;
    scope_ = new_scope(p->scope, p);
    loops_.clear();
    defer_depth_ = 0;
    push_depth_ = 0;
    macros_.clear();
    for (size_t i = 0; i < p->params.size(); ++i) {
      Decl* d = p->params[i];
      Symbol* s = declare(scope_, d->names[0].name, SymKind::Var, d->names[0].loc);
      s->type = p->type->params[i];
      s->owner = p;
      s->decl = d;
      s->state = Symbol::State::Resolved;
      complete_type(s->type, d->loc);
      d->syms = {s};
      if (d->is_using) add_using_var(s, d->loc);
    }
    for (Type* r : p->type->returns) complete_type(r, p->loc);
    check_block(p->body, /*new_scope=*/false);
    if (!p->type->returns.empty() && !always_returns(p->body)) {
      fail(p->loc, "procedure '", p->name.empty() ? "<anonymous>" : p->name,
           "' does not return a value on all paths");
    }
    restore(c);
  }

  bool contains_break_to(Stmt* s, Stmt* loop) {
    if (!s) return false;
    switch (s->kind) {
      case NK::Break: return static_cast<BreakContinue*>(s)->target == loop;
      case NK::Insert: return contains_break_to(static_cast<Insert*>(s)->result, loop);
      case NK::PushContext: return contains_break_to(static_cast<PushContext*>(s)->body, loop);
      case NK::ExprStmt: {
        Expr* e = static_cast<ExprStmt*>(s)->expr;
        return e->kind == NK::Expansion && contains_break_to(static_cast<MacroExpansion*>(e)->body, loop);
      }
      case NK::Block:
        for (Stmt* x : static_cast<Block*>(s)->stmts)
          if (contains_break_to(x, loop)) return true;
        return false;
      case NK::If: {
        auto* i = static_cast<If*>(s);
        return contains_break_to(i->then_s, loop) || contains_break_to(i->else_s, loop);
      }
      case NK::Switch:
        for (auto& c : static_cast<Switch*>(s)->cases)
          for (Stmt* x : c.body)
            if (contains_break_to(x, loop)) return true;
        return false;
      case NK::While: return contains_break_to(static_cast<While*>(s)->body, loop);
      case NK::For: {
        auto* f = static_cast<For*>(s);
        return contains_break_to(f->expansion ? f->expansion : f->body, loop);
      }
      case NK::Remove: return contains_break_to(static_cast<Remove*>(s)->lowered, loop);
      case NK::StaticIf: {
        auto* si = static_cast<StaticIf*>(s);
        for (Stmt* x : si->taken == 1 ? si->then_s : si->else_s)
          if (contains_break_to(x, loop)) return true;
        return false;
      }
      default: return false;
    }
  }

  // Whether `s` always returns from the procedure (or, with `exp`, leaves
  // that macro expansion with a value).
  bool always_returns(Stmt* s, MacroExpansion* exp = nullptr) {
    if (!s) return false;
    switch (s->kind) {
      case NK::Return: {
        MacroExpansion* t = static_cast<Return*>(s)->expand_target;
        return t == nullptr || t == exp;
      }
      case NK::Insert: return always_returns(static_cast<Insert*>(s)->result, exp);
      case NK::PushContext: return always_returns(static_cast<PushContext*>(s)->body, exp);
      case NK::Block:
        for (Stmt* x : static_cast<Block*>(s)->stmts)
          if (always_returns(x, exp)) return true;
        return false;
      case NK::If: {
        auto* i = static_cast<If*>(s);
        return i->else_s && always_returns(i->then_s, exp) && always_returns(i->else_s, exp);
      }
      case NK::Switch: {
        auto* sw = static_cast<Switch*>(s);
        bool has_default = false;
        for (auto& c : sw->cases) {
          if (c.values.empty()) has_default = true;
          if (c.fallthrough) continue;
          bool r = false;
          for (Stmt* x : c.body)
            if (always_returns(x, exp)) r = true;
          if (!r) return false;
        }
        return has_default;
      }
      case NK::While: {
        auto* w = static_cast<While*>(s);
        return w->cond->is_const() && w->cond->cv.kind == ConstValue::Bool && w->cond->cv.b &&
               !contains_break_to(w->body, w);
      }
      case NK::StaticIf: {
        auto* si = static_cast<StaticIf*>(s);
        for (Stmt* x : si->taken == 1 ? si->then_s : si->else_s)
          if (always_returns(x, exp)) return true;
        return false;
      }
      case NK::ExprStmt: {
        // A call to exit() never returns.
        Expr* e = static_cast<ExprStmt*>(s)->expr;
        // A macro expansion whose every path does `return.
        if (e->kind == NK::Expansion) return always_returns(static_cast<MacroExpansion*>(e)->body, exp);
        return e->kind == NK::Call && static_cast<Call*>(e)->builtin == Builtin::Exit;
      }
      default: return false;
    }
  }

  // ------------------------------------------------------------------ statements

  void check_block(Block* b, bool make_scope = true) {
    Scope* saved = scope_;
    if (make_scope) scope_ = new_scope(scope_, proc_);
    b->scope = scope_;
    for (Stmt* s : b->stmts) check_stmt(s);
    scope_ = saved;
  }

  // Bodies of if/while/for get their own scope even when they are single statements.
  void check_body(Stmt* s) {
    if (s->kind == NK::Block) {
      check_block(static_cast<Block*>(s));
      return;
    }
    Scope* saved = scope_;
    scope_ = new_scope(scope_, proc_);
    check_stmt(s);
    scope_ = saved;
  }

  void check_stmt(Stmt* s) {
    switch (s->kind) {
      case NK::Block: check_block(static_cast<Block*>(s)); return;
      case NK::ExprStmt: {
        auto* es = static_cast<ExprStmt*>(s);
        if (es->expr->kind == NK::Directive) {
          check_directive_stmt(s);
          return;
        }
        check_rv_allow_void(es->expr);
        return;
      }
      case NK::Decl: check_local_decl(static_cast<Decl*>(s)); return;
      case NK::Assign: check_assign(static_cast<Assign*>(s)); return;
      case NK::If: {
        auto* i = static_cast<If*>(s);
        check_cond(i->cond);
        check_body(i->then_s);
        if (i->else_s) check_body(i->else_s);
        return;
      }
      case NK::Switch: check_switch(static_cast<Switch*>(s)); return;
      case NK::While: {
        auto* w = static_cast<While*>(s);
        check_cond(w->cond);
        loops_.push_back(w);
        check_body(w->body);
        loops_.pop_back();
        return;
      }
      case NK::For: check_for(static_cast<For*>(s)); return;
      case NK::Return: check_return(static_cast<Return*>(s)); return;
      case NK::Break:
      case NK::Continue: {
        auto* bc = static_cast<BreakContinue*>(s);
        const char* what = s->kind == NK::Break ? "break" : "continue";
        if (defer_depth_ > 0) fail(s->loc, "'", what, "' is not allowed inside defer");
        if (!bc->label.empty()) {  // `break x` of a loop with a for_expansion: leaves the expansion
          for (auto it = remove_targets_.rbegin(); it != remove_targets_.rend(); ++it) {
            if (it->loop->it_name != bc->label) continue;
            if (!it->expanded) break;
            if (s->kind == NK::Continue)
              fail(s->loc, "'continue ", bc->label, "' of a loop with a for_expansion is not supported yet");
            auto x = for_expansions_.find(it->loop);
            if (x == for_expansions_.end()) break;
            bc->leave = x->second;
            return;
          }
        }
        if (loops_.empty()) fail(s->loc, "'", what, "' outside of a loop");
        if (bc->label.empty()) {
          bc->target = loops_.back();
        } else {
          for (auto it = loops_.rbegin(); it != loops_.rend(); ++it) {
            if ((*it)->kind == NK::For && static_cast<For*>(*it)->it_name == bc->label) {
              bc->target = *it;
              break;
            }
          }
          if (!bc->target) fail(s->loc, "no enclosing loop with iterator named '", bc->label, "'");
        }
        return;
      }
      case NK::Defer: {
        auto* d = static_cast<Defer*>(s);
        if (d->backtick) {
          const MacroCtx& m = top_level_macro(s->loc, "`defer");
          if (m.caller_in_run_operand) fail(s->loc, "`defer is not allowed in a macro expanded in a #run expression");
          d->target = m.exp;
        }
        ++defer_depth_;
        check_body(d->body);
        --defer_depth_;
        return;
      }
      case NK::Insert: check_insert(static_cast<Insert*>(s)); return;
      case NK::Remove: check_remove(static_cast<Remove*>(s)); return;
      case NK::StaticIf: {
        auto* si = static_cast<StaticIf*>(s);
        bool taken = eval_static_cond(si->cond);
        si->taken = taken ? 1 : 0;
        for (Stmt* x : taken ? si->then_s : si->else_s) check_stmt(x);
        return;
      }
      case NK::Using: check_using(static_cast<UsingStmt*>(s)); return;
      case NK::PushContext: check_push_context(static_cast<PushContext*>(s)); return;
      case NK::Import:
      case NK::Load:
        fail(s->loc, "#import and #load are only allowed at top level");
      default:
        fail(s->loc, "unexpected statement");
    }
  }

  void check_local_decl(Decl* d) {
    Scope* into = scope_;  // `x := ...; in a macro: the caller's scope
    bool nested_backtick = false;
    if (d->backtick) {
      if (macros_.empty()) fail(d->loc, "a backtick declaration is only allowed in the body of an #expand macro");
      const MacroCtx& m = macros_.back();
      if (m.caller_scope->is_module || !m.caller_scope->proc)
        fail(d->loc, "a backtick declaration needs a caller inside a procedure");
      // A for_expansion's `it and `it_index take the names of `for x, i: ...`.
      for (DeclName& n : d->names) {
        if (n.name == "it" && !m.rename_it.empty()) n.name = m.rename_it;
        else if (n.name == "it_index" && !m.rename_index.empty()) n.name = m.rename_index;
      }
      if (scope_ == m.body_scope) {
        into = m.caller_scope;
      } else {
        // Nested in the macro body (e.g. in its loop): declared there, and
        // visible to the caller's Code inserted within that block.
        if (d->is_const) fail(d->loc, "a backtick constant must be at the top level of the macro body");
        nested_backtick = true;
      }
    }
    if (d->is_const) {
      if (d->values.size() != d->names.size()) fail(d->loc, "constant declaration needs one value per name");
      for (size_t i = 0; i < d->names.size(); ++i) {
        Symbol* s = declare_const(into, d, i);
        s->decl = d;
        s->decl_index = (int)i;
        d->syms.push_back(s);
        resolve(s);
      }
      return;
    }
    Type* declared = d->type_expr ? resolve_type(d->type_expr) : nullptr;
    d->declared_type = declared;
    std::vector<Type*> types(d->names.size(), declared);
    if (!d->values.empty()) {
      if (d->values.size() == 1 && d->names.size() > 1) {
        // a, b := multi_return();
        Type* t = check(d->values[0], nullptr);
        if (t->kind != TypeKind::Tuple) fail(d->loc, "expected ", d->names.size(), " values, got 1");
        if (t->elems.size() < d->names.size())
          fail(d->loc, "procedure returns ", t->elems.size(), " values but ", d->names.size(), " are declared");
        for (size_t i = 0; i < d->names.size(); ++i) {
          if (declared && t->elems[i] != declared && !implicit_ok(t->elems[i], declared))
            fail(d->loc, "cannot assign ", type_name(t->elems[i]), " to ", type_name(declared));
          if (!declared) types[i] = t->elems[i];
        }
      } else {
        if (d->values.size() != d->names.size())
          fail(d->loc, "expected ", d->names.size(), " values, got ", d->values.size());
        for (size_t i = 0; i < d->names.size(); ++i) {
          check_value(d->values[i], declared);
          if (!declared) types[i] = d->values[i]->type;
          if (types[i]->kind == TypeKind::Null) fail(d->values[i]->loc, "cannot infer a type from null");
        }
      }
    } else if (!declared) {
      fail(d->loc, "declaration needs a type or a value");
    }
    for (size_t i = 0; i < d->names.size(); ++i) {
      if (types[i]->kind == TypeKind::Void) fail(d->loc, "variable cannot have type void");
      if (types[i]->kind == TypeKind::TypeT && !d->values.empty() && d->values.size() == d->names.size() &&
          d->values[i]->is_const()) {
        // `T := int;` is legal (a runtime Type value) - keep it as a variable.
      }
      complete_type(types[i], d->loc);
      Symbol* s = declare(into, d->names[i].name, SymKind::Var, d->names[i].loc);
      s->type = types[i];
      s->owner = proc_;
      s->decl = d;
      s->decl_index = (int)i;
      s->state = Symbol::State::Resolved;
      s->align = align_value(d->align);
      d->syms.push_back(s);
      if (nested_backtick) nested_backticks_.push_back({macros_.back().id, s});
      if (d->is_using) add_using_var(s, d->loc);
    }
    if (d->names.size() == 1) d->declared_type = types[0];
  }

  void add_using_var(Symbol* s, SourceLoc loc) {
    Type* t = s->type->is_pointer() ? s->type->elem : s->type;
    if (t->kind != TypeKind::Struct)
      fail(loc, "'using' needs a struct or a pointer to a struct, not ", type_name(s->type));
    complete_type(t, loc);
    scope_->usings.push_back({s, nullptr});
  }

  void check_using(UsingStmt* u) {
    Expr*& e = u->target;
    if (e->kind == NK::Ident) {
      check(e, nullptr);
      auto* id = static_cast<Ident*>(e);
      if (id->sym->kind == SymKind::Var) return add_using_var(id->sym, u->loc);
    } else {
      check(e, nullptr);
    }
    if (e->is_const() && e->cv.kind == ConstValue::TypeV && e->cv.t->is_enum()) {
      scope_->usings.push_back({nullptr, e->cv.t});
      return;
    }
    fail(u->loc, "'using' needs a variable or an enum type");
  }

  void require_lvalue(Expr* e, const char* what) {
    if (e->kind == NK::Ident && static_cast<Ident*>(e)->sym && static_cast<Ident*>(e)->sym->kind == SymKind::Const)
      fail(e->loc, "cannot ", what, " constant '", static_cast<Ident*>(e)->name, "'");
    if (!is_lvalue(e)) fail(e->loc, "cannot ", what, " this expression (not an lvalue)");
  }

  void check_assign(Assign* a) {
    // v[i] op= x with `operator []` and `operator []=`: v[i] = v[i] op x
    if (a->op != Tok::Eq && a->lhs.size() == 1 && a->rhs.size() == 1 && a->lhs[0]->kind == NK::Index) {
      auto* ix = static_cast<Index*>(a->lhs[0]);
      Type* bt = check_value(ix->base, nullptr);
      if (bt->kind == TypeKind::Struct) {
        Expr* elem = operator_call("*[]", {ix->base, ix->index}, ix->loc);
        if (elem) {
          Expr* d = ast_.make<Unary>(ix->loc, UnOp::Deref, elem);
          check(d, nullptr);
          a->lhs[0] = d;
        } else {
          std::string op = tok_name(a->op);
          op = op.substr(1, op.size() - 3);  // "'+='" -> "+"
          BinOp bop = BinOp::Add;
          for (int k = 0; k <= (int)BinOp::LogOr; ++k)
            if (op == binop_str((BinOp)k)) bop = (BinOp)k;
          auto* read = ast_.make<Index>(ix->loc, ix->base, ix->index);
          a->rhs[0] = ast_.make<Binary>(a->loc, bop, read, a->rhs[0]);
          a->op = Tok::Eq;
        }
      }
    }
    // v[i] = x with `operator []=` (and no `operator *[]`, which makes v[i] an lvalue)
    if (a->op == Tok::Eq && a->lhs.size() == 1 && a->rhs.size() == 1 && a->lhs[0]->kind == NK::Index) {
      auto* ix = static_cast<Index*>(a->lhs[0]);
      Type* bt = check_value(ix->base, nullptr);
      if (bt->kind == TypeKind::Struct) {
        if (Expr* p = operator_call("*[]", {ix->base, ix->index}, ix->loc)) {
          if (!p->type->is_pointer()) fail(ix->loc, "'operator *[]' must return a pointer");
          Expr* d = ast_.make<Unary>(ix->loc, UnOp::Deref, p);
          check(d, nullptr);
          a->lhs[0] = d;
        } else {
          if ((a->overload_call = operator_call("[]=", {ix->base, ix->index, a->rhs[0]}, ix->loc))) return;
          fail(ix->loc, "cannot assign to an element of ", type_name(bt), " (no 'operator []=' or 'operator *[]' fits)");
        }
      }
    }
    if (a->op == Tok::Eq) {
      for (Expr*& l : a->lhs) {
        check_rv(l, nullptr);
        require_lvalue(l, "assign to");
      }
      if (a->rhs.size() == 1 && a->lhs.size() > 1) {
        Type* t = check(a->rhs[0], nullptr);
        if (t->kind != TypeKind::Tuple || t->elems.size() < a->lhs.size())
          fail(a->loc, "expected ", a->lhs.size(), " values on the right side of '='");
        for (size_t i = 0; i < a->lhs.size(); ++i)
          if (t->elems[i] != a->lhs[i]->type && !implicit_ok(t->elems[i], a->lhs[i]->type))
            fail(a->loc, "cannot assign ", type_name(t->elems[i]), " to ", type_name(a->lhs[i]->type));
        return;
      }
      if (a->rhs.size() != a->lhs.size())
        fail(a->loc, "assignment has ", a->lhs.size(), " targets but ", a->rhs.size(), " values");
      for (size_t i = 0; i < a->lhs.size(); ++i) check_value(a->rhs[i], a->lhs[i]->type);
      return;
    }
    if (a->lhs.size() != 1 || a->rhs.size() != 1) fail(a->loc, "compound assignment takes exactly one target");
    Expr*& l = a->lhs[0];
    Expr*& r = a->rhs[0];
    Type* lt = check_rv(l, nullptr);
    require_lvalue(l, "assign to");
    if (overloadable(lt)) {
      std::string op = tok_name(a->op);  // "'+='"
      op = op.substr(1, op.size() - 2);
      check_rv(r, lt);
      // `operator +=` if there is one, else v = v + x with `operator +`.
      if ((a->overload_call = operator_call(op, {l, r}, a->loc))) return;
      std::string bin = op.substr(0, op.size() - 1);
      if (Expr* call = operator_call(bin, {l, r}, a->loc)) {
        a->op = Tok::Eq;
        r = call;
        coerce(r, lt);
        return;
      }
      fail(a->loc, "no 'operator ", op, "' or 'operator ", bin, "' for ", type_name(lt), " and ", type_name(r->type));
    }
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
      case Tok::ShrEq: op = BinOp::Shr; break;
      default: fail(a->loc, "bad assignment operator");
    }
    if (lt->is_pointer() && (op == BinOp::Add || op == BinOp::Sub)) {
      check_value(r, nullptr);
      if (!r->type->is_integer()) fail(r->loc, "pointer arithmetic needs an integer offset");
      return;
    }
    if (op == BinOp::Shl || op == BinOp::Shr) {
      if (lt->kind != TypeKind::Int) fail(l->loc, "shift needs an integer");
      check_value(r, nullptr);
      if (r->type->kind != TypeKind::Int) fail(r->loc, "shift amount must be an integer");
      return;
    }
    check_value(r, lt);
    check_binop_types(op, lt, a->loc);
  }

  void check_switch(Switch* sw) {
    Type* t = check_value(sw->subject, nullptr);
    if (!t->is_intlike() && t->kind != TypeKind::Bool && t->kind != TypeKind::String)
      fail(sw->subject->loc, "'if ==' switch needs an integer, enum, bool or string, not ", type_name(t));
    std::unordered_set<std::string> seen;
    bool has_default = false;
    for (auto& c : sw->cases) {
      if (c.values.empty()) {
        if (has_default) fail(c.loc, "duplicate default case");
        has_default = true;
      }
      for (Expr*& v : c.values) {
        check_value(v, t);
        if (!v->is_const()) fail(v->loc, "case value must be a compile-time constant");
        std::string key = v->cv.kind == ConstValue::Bool     ? (v->cv.b ? "1" : "0")
                          : v->cv.kind == ConstValue::String ? "s" + v->cv.s
                                                             : i128_str(v->cv.i);
        if (!seen.insert(key).second) fail(v->loc, "duplicate case value");
      }
      Scope* saved = scope_;
      scope_ = new_scope(scope_, proc_);
      c.scope = scope_;
      for (Stmt* s : c.body) check_stmt(s);
      scope_ = saved;
    }
    if (sw->complete && t->is_enum()) {
      for (auto& m : t->members)
        if (!seen.count(i128_str(m.value)))
          fail(sw->loc, "#complete switch is missing enum member '", m.name, "'");
    }
  }

  void check_for(For* f) {
    Type* it_type = nullptr;
    if (f->range_lo) {
      if (f->by_pointer) fail(f->loc, "cannot iterate a range by pointer");
      Type* lt = check_rv(f->range_lo, nullptr);
      Type* ht = check_rv(f->range_hi, nullptr);
      if (lt->is_untyped() && ht->is_untyped()) {
        coerce(f->range_lo, T.s64);
        coerce(f->range_hi, T.s64);
      } else if (lt->is_untyped()) {
        coerce(f->range_lo, ht);
      } else if (ht->is_untyped()) {
        coerce(f->range_hi, lt);
      } else if (lt != ht) {
        if (implicit_ok(lt, ht)) coerce(f->range_lo, ht);
        else if (implicit_ok(ht, lt)) coerce(f->range_hi, lt);
        else fail(f->loc, "range bounds have different types: ", type_name(lt), " and ", type_name(ht));
      }
      it_type = f->range_lo->type;
      if (it_type->kind != TypeKind::Int) fail(f->loc, "range bounds must be integers, not ", type_name(it_type));
    } else {
      Type* t = check_value(f->iterable, nullptr);
      if (t->kind != TypeKind::Array) {
        expand_for(f, t);
        return;
      }
      if (f->by_pointer && t->is_fixed_array() && !is_lvalue(f->iterable))
        fail(f->iterable->loc, "cannot iterate by pointer over a temporary array");
      it_type = f->by_pointer ? T.pointer_to(t->elem) : t->elem;
    }
    Scope* saved = scope_;
    scope_ = new_scope(scope_, proc_);
    f->scope = scope_;
    f->it_sym = declare(scope_, f->it_name, SymKind::Var, f->loc);
    f->it_sym->type = it_type;
    f->it_sym->owner = proc_;
    f->it_sym->state = Symbol::State::Resolved;
    if (f->index_name != f->it_name) {
      f->index_sym = declare(scope_, f->index_name, SymKind::Var, f->loc);
      f->index_sym->type = T.s64;
      f->index_sym->owner = proc_;
      f->index_sym->state = Symbol::State::Resolved;
    }
    loops_.push_back(f);
    remove_targets_.push_back({f, false, 0});
    check_body(f->body);
    remove_targets_.pop_back();
    loops_.pop_back();
    scope_ = saved;
  }

  // `for x, i: value { body }` over a value that is not an array: a call of
  // the #expand macro for_expansion(value, #code body, flags), as in Jai. The
  // macro declares `it and `it_index (here x and i) and inserts the body;
  // flags (For_Flags: POINTER for `for *`, REVERSE for `for <`) is a constant
  // in it. A first parameter that is a pointer gets the value's address.
  void expand_for(For* f, Type* t) {
    std::vector<Symbol*> cands;
    Builtin no_builtin = Builtin::None;
    gather_procs("for_expansion", cands, no_builtin);
    if (cands.empty())
      fail(f->iterable->loc, "cannot iterate over a value of type ", type_name(t),
           " (it is not an array, and no for_expansion is declared for it)");
    auto flags_arg = [&] {
      int v = (f->by_pointer ? 1 : 0) | (f->reverse ? 2 : 0);
      auto* e = ast_.make<IntLit>(f->loc, (uint64_t)v);
      e->type = for_flags_t_;
      e->cv = ConstValue::of_int(v);
      return e;
    };
    // The value's address when an overload takes a pointer to it (the usual
    // `for_expansion :: (list: *List, ...)`), else the value. Only the
    // signatures are matched here.
    Expr* subject = f->iterable;
    if (!t->is_pointer() && is_lvalue(subject)) {
      auto* addr = ast_.make<Unary>(subject->loc, UnOp::AddrOf, subject);
      Expr* probe_addr = addr;
      check(probe_addr, nullptr);
      auto* probe = ast_.make<Call>(f->loc, ast_.make<Ident>(f->loc, "for_expansion"));
      probe->args.push_back(Arg{"", f->loc, probe_addr});
      probe->args.push_back(Arg{"", f->loc, ast_.make<CodeLit>(f->loc)});
      probe->args.push_back(Arg{"", f->loc, flags_arg()});
      bool fits = false;
      Context ctx = save();
      try {
        fits = pick_overload(probe, cands, "for_expansion", /*builtin_fallback=*/true) != nullptr;
      } catch (const CompileError&) {
      }
      restore(ctx);
      if (fits) subject = ast_.make<Unary>(subject->loc, UnOp::AddrOf, subject);
    }
    Block* body;
    if (f->body->kind == NK::Block) {
      body = static_cast<Block*>(f->body);
    } else {
      body = ast_.make<Block>(f->body->loc);
      body->stmts.push_back(f->body);
    }
    auto* code = ast_.make<CodeLit>(f->body->loc);
    code->code_block = body;
    Expr* flags = flags_arg();
    auto* callee = ast_.make<Ident>(f->loc, "for_expansion");
    auto* call = ast_.make<Call>(f->loc, callee);
    call->args.push_back(Arg{"", f->loc, subject});
    call->args.push_back(Arg{"", f->loc, code});
    call->args.push_back(Arg{"", f->loc, flags});
    Expr* e = call;
    For* saved_pending = pending_for_;
    pending_for_ = f;
    remove_targets_.push_back({f, true, insert_removes_.size()});
    Scope* saved_scope = scope_;
    scope_ = new_scope(scope_, proc_);  // the loop's own scope: `it and `it_index go here
    f->scope = scope_;
    with_note(cat("in the for_expansion for ", type_name(t), " (the for at ", sm_.describe(f->loc), ")"), [&] {
      check(e, nullptr);
      return 0;
    });
    scope_ = saved_scope;
    remove_targets_.pop_back();
    pending_for_ = saved_pending;
    if (e->kind != NK::Expansion)
      fail(f->loc, "for_expansion for ", type_name(t), " must be an #expand macro");
    f->expansion = ast_.make<ExprStmt>(f->loc, e);
  }

  // `remove x;`
  void check_remove(Remove* r) {
    for (auto it = remove_targets_.rbegin(); it != remove_targets_.rend(); ++it) {
      For* f = it->loop;
      if (f->it_name != r->name) continue;
      if (!it->expanded) {
        Type* at = f->iterable ? f->iterable->type : nullptr;
        if (!at) fail(r->loc, "cannot remove from a range ('", r->name, "' is a number)");
        if (at->is_fixed_array()) fail(r->loc, "cannot remove from a fixed array (", type_name(at), ")");
        if (!is_lvalue(f->iterable))
          fail(r->loc, "cannot remove from this array: it is not a variable (or other lvalue)");
        if (f->reverse && f->by_pointer) {}  // fine
        r->target = f;
        f->has_remove = true;
        return;
      }
      // A for_expansion: the code of the #insert (remove = { ... }) that inserted this body.
      if (insert_removes_.size() <= it->inserts_depth)
        fail(r->loc, "the for_expansion of this loop does not support 'remove' (it inserts the body without "
                     "#insert (remove = { ... }) body;)");
      const InsertRemove& ir = insert_removes_.back();
      r->lowered = static_cast<Block*>(clone_stmt(ast_, ir.code));
      ContextGuard guard(*this);
      restore(ir.ctx);
      in_run_operand_ = ir.in_run_operand;
      check_block(r->lowered);
      return;
    }
    fail(r->loc, "'remove ", r->name, "': no enclosing for loop with iterator '", r->name, "'");
  }

  void check_return(Return* r) {
    if (defer_depth_ > 0) fail(r->loc, "'return' is not allowed inside defer");
    // A plain return in a macro body leaves the expansion; `return leaves
    // the macro's caller (a procedure, or the expansion of an outer macro).
    size_t level = macros_.size();
    if (r->backtick) {
      if (level == 0) fail(r->loc, "`return is only allowed in the body of an #expand macro");
      const MacroCtx& m = macros_[--level];
      if (m.caller_defer_depth > 0) fail(r->loc, "`return is not allowed in a macro expanded inside defer");
    }
    bool run_operand = level < macros_.size() ? macros_[level].caller_in_run_operand : in_run_operand_;
    if (level > 0) {
      r->expand_target = macros_[level - 1].exp;
    } else {
      if (run_operand) fail(r->loc, "'return' is not allowed in a #run expression");
      if (!proc_) fail(r->loc, "'return' outside of a procedure");
    }
    const auto& rets = r->expand_target ? r->expand_target->macro->type->returns : proc_->type->returns;
    if (r->values.size() == 1 && rets.size() > 1) {
      Type* t = check(r->values[0], nullptr);
      if (t->kind == TypeKind::Tuple && t->elems == rets) return;
    }
    if (r->values.size() != rets.size()) {
      fail(r->loc, "procedure returns ", rets.size(), " value", rets.size() == 1 ? "" : "s", ", but ",
           r->values.size(), " ", r->values.size() == 1 ? "was" : "were", " given");
    }
    for (size_t i = 0; i < rets.size(); ++i) check_value(r->values[i], rets[i]);
  }

  // ------------------------------------------------------------------ conversions

  bool implicit_ok(Type* from, Type* to) {
    if (from == to) return true;
    if (to->kind == TypeKind::Any) return from->kind != TypeKind::Void && from->kind != TypeKind::Tuple;
    // A distinct type converts to and from nothing implicitly (cast or xx it);
    // an isa type converts to its base (and so to what the base converts to).
    if (from->isa && !to->distinct_of) return implicit_ok(from->distinct_of, to);
    if (from->isa && to->distinct_of) {
      for (Type* b = from->distinct_of; b; b = b->isa ? b->distinct_of : nullptr)
        if (b == to) return true;
      return false;
    }
    if (from->distinct_of || to->distinct_of) return false;
    if (from->kind == TypeKind::Int && to->kind == TypeKind::Int) {
      if (from->is_signed == to->is_signed) return from->bits < to->bits;
      return !from->is_signed && to->is_signed && from->bits < to->bits;
    }
    if (from == T.f32 && to == T.f64) return true;  // (distinct types never get here)
    if (from->is_pointer() && to->is_pointer()) {
      if (to->elem == prog_.type_info.info && is_type_info_struct(from->elem)) return true;  // *Type_Info_Struct -> *Type_Info
      return to->elem->kind == TypeKind::Void || from->elem->kind == TypeKind::Void;
    }
    // A Type is a pointer to its Type_Info.
    if (from->kind == TypeKind::TypeT && to->is_pointer() && to->elem == prog_.type_info.info) return true;
    if (is_type_info_ptr(from) && to->kind == TypeKind::TypeT) return true;
    if (from->kind == TypeKind::Array && to->is_view()) {
      return from->array_kind != ArrayKind::View && from->elem == to->elem;
    }
    return false;
  }

  void coerce(Expr*& e, Type* to) {
    Type* from = e->type;
    if (from == to) return;
    if (to->kind == TypeKind::Any) {
      if (from->kind == TypeKind::Code)
        fail(e->loc, "a Code value cannot be converted to Any (Code only exists at compile time)");
      if (from->is_untyped()) default_untyped(e);
      if (e->type->kind == TypeKind::Any) return;
      if (e->type->kind == TypeKind::Void) fail(e->loc, "cannot pass a void expression as Any");
      e = ast_.make<ImplicitConv>(e->loc, e, to);
      return;
    }
    if (from->kind == TypeKind::UntypedInt) {
      if (to->kind == TypeKind::Int) {
        if (!int_fits(e->cv.i, to))
          fail(e->loc, "constant ", i128_str(e->cv.i), " does not fit in ", type_name(to));
        e->type = to;
        return;
      }
      if (to->kind == TypeKind::Float) {
        e->cv = ConstValue::of_float((double)e->cv.i);
        if (to->is_f32()) e->cv.f = (float)e->cv.f;
        e->type = to;
        return;
      }
      if (to->is_enum() && to->is_flags) {
        // enum_flags accept raw integer constants (e.g. `(f & .A) != 0`).
        if (!int_fits(e->cv.i, to->elem))
          fail(e->loc, "constant ", i128_str(e->cv.i), " does not fit in ", type_name(to));
        e->type = to;
        return;
      }
      fail(e->loc, "cannot use an integer constant as ", type_name(to));
    }
    if (from->kind == TypeKind::UntypedFloat) {
      if (to->kind == TypeKind::Float) {
        if (to->is_f32()) e->cv.f = (float)e->cv.f;
        e->type = to;
        return;
      }
      fail(e->loc, "cannot use a float constant as ", type_name(to));
    }
    if (from->kind == TypeKind::Null) {
      if (to->is_pointer() || to->is_proc()) {
        e->type = to;
        return;
      }
      fail(e->loc, "cannot use null as ", type_name(to));
    }
    if (implicit_ok(from, to)) {
      e = ast_.make<ImplicitConv>(e->loc, e, to);
      return;
    }
    fail(e->loc, "type mismatch: expected ", type_name(to), ", got ", type_name(from));
  }

  void default_untyped(Expr*& e) {
    switch (e->type->kind) {
      case TypeKind::UntypedInt:
        if (!int_fits(e->cv.i, T.s64))
          fail(e->loc, "constant ", i128_str(e->cv.i), " does not fit in int (s64); cast it to u64");
        e->type = T.s64;
        return;
      case TypeKind::UntypedFloat:
        e->type = T.f32;  // Jai float literals default to float32
        e->cv.f = (float)e->cv.f;
        return;
      case TypeKind::Null:
        e->type = T.void_ptr;
        return;
      default:
        return;
    }
  }

  // ------------------------------------------------------------------ expressions

  // Checks an expression; the result may be untyped, a tuple, or void.
  Type* check(Expr*& e, Type* hint) {
    // Checking is idempotent: an expression is checked once (e.g. call
    // arguments are checked before an overload is chosen).
    if (e->type) return e->type;
    Type* t = check_inner(e, hint);
    e->type = t;
    return t;
  }

  // Checks an expression used as a single value (extracts the first value of a
  // multi-value call); the result may still be untyped.
  Type* check_rv(Expr*& e, Type* hint) {
    Type* t = check(e, hint);
    if (t->kind == TypeKind::Tuple) {
      auto* g = ast_.make<TupleGet>(e->loc, e, 0);
      g->type = t->elems[0];
      e = g;
      t = g->type;
    }
    if (t->kind == TypeKind::Void) fail(e->loc, "expression does not produce a value");
    return t;
  }

  void check_rv_allow_void(Expr*& e) { check(e, nullptr); }

  // Checks an expression used as a value of type `target` (or its natural type
  // if `target` is null), inserting conversions as needed.
  Type* check_value(Expr*& e, Type* target) {
    check_rv(e, target);
    if (target) coerce(e, target);
    else default_untyped(e);
    return e->type;
  }

  void check_cond(Expr*& e) {
    Type* t = check_value(e, nullptr);
    switch (t->kind) {
      case TypeKind::Bool: case TypeKind::Int: case TypeKind::Pointer: case TypeKind::Enum:
      case TypeKind::Proc: case TypeKind::Float:
        return;
      default:
        fail(e->loc, "condition must be a bool, number or pointer, not ", type_name(t));
    }
  }

  Type* resolve_type(Expr*& e) {
    check(e, nullptr);
    if (!e->is_const() || e->cv.kind != ConstValue::TypeV) fail(e->loc, "expected a type");
    return e->cv.t;
  }

  bool is_lvalue(Expr* e) {
    switch (e->kind) {
      case NK::Ident: return static_cast<Ident*>(e)->sym->kind == SymKind::Var;
      case NK::Unary: return static_cast<Unary*>(e)->op == UnOp::Deref;
      case NK::Index: {
        auto* i = static_cast<Index*>(e);
        if (i->base->type->is_fixed_array()) return is_lvalue(i->base);
        return true;
      }
      case NK::Member: {
        auto* m = static_cast<Member*>(e);
        switch (m->mkind) {
          case MemberKind::Field:
          case MemberKind::ArrayCount:
          case MemberKind::ArrayData:
          case MemberKind::ArrayAllocated:
          case MemberKind::ArrayAllocator:
          case MemberKind::StringCount:
          case MemberKind::StringData:
            if (m->base->type->is_fixed_array() && !m->through_pointer) return false;
            return m->through_pointer || is_lvalue(m->base);
          default:
            return false;
        }
      }
      default: return false;
    }
  }

  Type* check_inner(Expr*& e, Type* hint) {
    switch (e->kind) {
      case NK::IntLit: {
        auto* l = static_cast<IntLit*>(e);
        e->cv = ConstValue::of_int((i128)l->value);
        return T.untyped_int;
      }
      case NK::FloatLit:
        e->cv = ConstValue::of_float(static_cast<FloatLit*>(e)->value);
        return T.untyped_float;
      case NK::StringLit:
        e->cv = ConstValue::of_string(static_cast<StringLit*>(e)->value);
        return T.string_t;
      case NK::BoolLit:
        e->cv = ConstValue::of_bool(static_cast<BoolLit*>(e)->value);
        return T.bool_t;
      case NK::NullLit:
        e->cv = ConstValue::null();
        return T.null_t;
      case NK::Ident: {
        auto* id = static_cast<Ident*>(e);
        if (id->backtick && !id->sym) return check_backtick_name(e, hint);
        return check_name(e, hint);
      }
      case NK::Unary: {
        auto* u = static_cast<Unary*>(e);
        Type* t = check_unary(u, hint);
        if (u->overload) {
          e = u->overload;
          return e->type;
        }
        return t;
      }
      case NK::Binary: {
        auto* b = static_cast<Binary*>(e);
        Type* t = check_binary(b, hint);
        if (b->overload) {  // rewritten into a call of `operator<op>`
          e = b->overload;
          return e->type;
        }
        return t;
      }
      case NK::Call: {
        auto* c = static_cast<Call*>(e);
        Type* t = check_call(c, hint);
        if (c->expansion) e = c->expansion;  // a call of an #expand macro
        return t;
      }
      case NK::Index: {
        auto* ix = static_cast<Index*>(e);
        Type* t = check_index(ix);
        if (ix->overload) {
          e = ix->overload;
          return e->type;
        }
        return t;
      }
      case NK::Member: {
        auto* m = static_cast<Member*>(e);
        if (m->base)
          if (Scope* ms = module_of(m->base)) {  // Module.name
            Symbol* s = module_member(ms, m);
            if (s->overloads.size() > 1) {
              Symbol* pick = nullptr;
              for (Symbol* o : s->overloads) {
                resolve(o);
                if (hint && o->type == hint) pick = o;
              }
              if (!pick) fail(m->loc, "'", m->name, "' is overloaded; the expected procedure type is needed to pick one");
              s = pick;
            }
            auto* id = ast_.make<Ident>(m->loc, m->name);
            id->sym = s;
            e = id;
            return check_ident(id, hint);
          }
        if (m->base)
          if (Symbol* s = member_constant(m)) {  // S.N / value.N: a constant of the struct
            auto* id = ast_.make<Ident>(m->loc, m->name);
            if (s->overloads.size() > 1) {
              Symbol* pick = nullptr;
              for (Symbol* o : s->overloads) {
                resolve(o);
                if (hint && o->type == hint) pick = o;
              }
              if (!pick) fail(m->loc, "'", m->name, "' is overloaded; the expected procedure type is needed to pick one");
              s = pick;
            }
            id->sym = s;
            e = id;
            return check_ident(id, hint);
          }
        return check_member(m, hint);
      }
      case NK::Cast: return check_cast(static_cast<Cast*>(e));
      case NK::AutoCast: {
        auto* a = static_cast<AutoCast*>(e);
        if (!hint || hint->is_untyped() || hint->kind == TypeKind::Any)
          fail(e->loc, "cannot infer the target type of 'xx' here");
        auto* c = ast_.make<Cast>(e->loc, nullptr, a->operand);
        Type* t = do_cast(c, hint);
        e = c;
        return t;
      }
      case NK::StructLit: return check_struct_lit(static_cast<StructLit*>(e), hint);
      case NK::ArrayLit: return check_array_lit(static_cast<ArrayLit*>(e), hint);
      case NK::ProcLit: {
        auto* p = static_cast<ProcLit*>(e);
        if (p->is_poly || p->poly_returns)
          fail(e->loc, "a polymorphic procedure must be declared as a named constant (name :: (x: $T) {...}); "
               "polymorphic procedure values are not supported yet");
        if (lambda_hint_ != hint) {  // (infer_poly sets the hint itself, with lambda_hint_results_)
          lambda_hint_ = hint && hint->is_proc() ? hint : nullptr;
          lambda_hint_results_ = true;
        }
        define_proc(p);
        return p->type;
      }
      case NK::StructDef:
        if (!static_cast<StructDef*>(e)->params.empty())
          fail(e->loc, "a polymorphic struct must be declared as a named constant (Name :: struct ($T: Type) {...})");
        e->cv = ConstValue::of_type(define_struct(static_cast<StructDef*>(e), ""));
        return T.type_t;
      case NK::EnumDef:
        e->cv = ConstValue::of_type(define_enum(static_cast<EnumDef*>(e), ""));
        return T.type_t;
      case NK::PtrType: {
        auto* p = static_cast<PtrType*>(e);
        e->cv = ConstValue::of_type(T.pointer_to(resolve_type(p->elem)));
        return T.type_t;
      }
      case NK::ArrType: return check_arr_type(static_cast<ArrType*>(e));
      case NK::ProcType: {
        auto* p = static_cast<ProcTypeExpr*>(e);
        std::vector<Type*> ps, rs;
        for (Expr*& x : p->params) ps.push_back(resolve_type(x));
        for (Expr*& x : p->returns) {
          Type* r = resolve_type(x);
          if (r->kind != TypeKind::Void) rs.push_back(r);
        }
        if (p->c_call) {
          if (p->vararg >= 0) fail(e->loc, "variadic #c_call procedure types are not supported");
          if (rs.size() > 1) fail(e->loc, "#c_call/#foreign procedures cannot return multiple values");
          for (size_t i = 0; i < ps.size(); ++i) check_c_type(ps[i], p->params[i]->loc, false);
          if (rs.size() == 1) check_c_type(rs[0], e->loc, true);
        }
        e->cv = ConstValue::of_type(T.proc(ps, rs, p->c_call, p->vararg));
        return T.type_t;
      }
      case NK::Ifx: return check_ifx(static_cast<Ifx*>(e), hint);
      case NK::ImplicitConv:
      case NK::TupleGet:
        return e->type;
      case NK::Library:
        fail(e->loc, "a library can only be bound to a constant");
      case NK::CodeLit: {
        auto* cl = static_cast<CodeLit*>(e);
        CodeEntry ce;
        ce.expr = cl->code_expr;
        ce.block = cl->code_block;
        ce.loc = cl->loc;
        ce.scope = scope_;
        ce.macro_id = macros_.empty() ? 0 : macros_.back().id;
        codes_.push_back(ce);
        e->cv = ConstValue::code((i128)codes_.size());
        return T.code_t;
      }
      case NK::Expansion:
        return e->type;
      case NK::Directive: {
        auto* d = static_cast<DirectiveExpr*>(e);
        if (d->name == "run") return check_run(d, hint, /*statement=*/false);
        if (d->name == "insert") return check_insert_expr(e, hint);
        if (d->name == "location") return check_location(d);
        if (d->name == "caller_location") {
          if (!in_param_default_)
            fail(e->loc, "#caller_location is only allowed as a parameter's default value (loc := #caller_location)");
          // Never used: every call that leaves the argument out passes its own
          // location instead (caller_location_at).
          e->type = source_loc_t_;
          e->cv = location_value(e->loc);
          return e->type;
        }
        if (d->name == "this") return check_this(e);
        if (d->name == "distinct" || d->name == "isa")
          fail(e->loc, "a distinct type must be declared as a named constant: Name :: #type,", d->name, " T;");
        if (d->name.rfind("import:", 0) == 0) fail(e->loc, "named #import is only supported as `Name :: #import \"...\";`");
        fail(e->loc, "#", d->name, " is not supported here");
      }
      default:
        fail(e->loc, "unexpected node in expression");
    }
  }

  // `#this`: the struct whose body it is in (in a polymorphic struct, the
  // instance), or else the procedure whose body it is in.
  Type* check_this(Expr*& e) {
    for (Scope* s = scope_; s; s = s->parent) {
      if (s->this_type) {
        e->cv = ConstValue::of_type(s->this_type);
        return T.type_t;
      }
      if (s->proc && (!s->parent || s->parent->proc != s->proc)) {  // the outermost scope of a procedure body
        if (!macros_.empty()) fail(e->loc, "#this is not supported in an #expand macro");
        ProcLit* p = s->proc;
        if (in_run_operand_) fail(e->loc, "#this cannot be used in a #run expression");
        want_proc(p);
        e = p;
        return p->type;
      }
    }
    fail(e->loc, "#this is only allowed inside a struct body or a procedure");
  }

  // A name: a symbol, or a member brought in by `using` (rewritten).
  Type* check_name(Expr*& e, Type* hint) {
    auto* id = static_cast<Ident*>(e);
    if (!id->sym) {
      Found f = find(id->name, id->loc);
      if (f.via) {  // a member of a `using` variable: rewrite to var.member
        auto* base = ast_.make<Ident>(id->loc, f.via->name);
        base->sym = f.via;
        auto* m = ast_.make<Member>(id->loc, base, id->name);
        e = m;
        return check_member(m, hint);
      }
      if (f.enum_type) {
        auto* m = ast_.make<Member>(id->loc, nullptr, id->name);
        e = m;
        return enum_member(m, f.enum_type);
      }
    }
    return check_ident(id, hint);
  }

  Type* check_ident(Ident* id, Type* hint) {
    if (id->poly && !id->sym && !find(id->name, id->loc).sym)
      fail(id->loc, "'$", id->name, "' can only appear in the parameter types of a procedure");
    Symbol* s = id->sym ? id->sym : lookup(id->name, id->loc);
    if (!id->sym && s->overloads.size() > 1) {  // a procedure value: the overload with the expected type
      Symbol* pick = nullptr;
      for (Symbol* o : s->overloads) {
        resolve(o);
        if (hint && o->type == hint) pick = o;
      }
      if (!pick) fail(id->loc, "'", id->name, "' is overloaded; the expected procedure type is needed to pick one");
      s = pick;
    }
    id->sym = s;
    if (s->is_context) return check_context_ident(id);
    resolve(s);
    switch (s->kind) {
      case SymKind::Var:
        if (in_run_operand_ && !s->is_global && proc_ && s->owner == proc_)
          fail(id->loc, "#run cannot use local variable '", id->name, "': it only exists when the program runs ",
               "(use a constant, or pass the value to a procedure called by #run)");
        return s->type;
      case SymKind::Const:
        id->cv = s->cv;
        return s->type;
      case SymKind::Type:
        id->cv = s->cv;
        return T.type_t;
      case SymKind::Proc:
        if (s->proc && s->proc->is_macro)
          fail(id->loc, "'", id->name, "' is an #expand macro; it can only be called, not used as a value");
        return s->type;
      case SymKind::Builtin: fail(id->loc, "'", id->name, "' is a built-in procedure and can only be called");
      case SymKind::Library: return T.void_t;  // only meaningful in #foreign
      case SymKind::Module: fail(id->loc, "module '", id->name, "' cannot be used as a value");
      case SymKind::PolyProc:
        fail(id->loc, "'", id->name, "' is a polymorphic procedure; using it as a value without calling it "
             "(polymorphic procedure values) is not supported yet");
      case SymKind::PolyStruct:
        fail(id->loc, "'", id->name, "' is a polymorphic struct; give its parameters: ", id->name, "(...)");
    }
    return T.void_t;
  }

  Type* check_arr_type(ArrType* a) {
    Type* elem = resolve_type(a->elem);
    if (elem->kind == TypeKind::Void) fail(a->loc, "array of void");
    int64_t n = 0;
    if (a->akind == ArrayKind::Fixed) {
      check_value(a->count, T.s64);
      if (!a->count->is_const()) fail(a->count->loc, "array size must be a compile-time constant");
      if (a->count->cv.i < 0) fail(a->count->loc, "array size cannot be negative");
      n = (int64_t)a->count->cv.i;
    }
    Type* t = T.array_of(elem, a->akind, n);
    a->cv = ConstValue::of_type(t);
    return T.type_t;
  }

  Type* check_unary(Unary* u, Type* hint) {
    switch (u->op) {
      case UnOp::AddrOf: {
        Type* t = check(u->operand, nullptr);
        if (t->kind == TypeKind::Void && u->operand->kind == NK::Member &&
            static_cast<Member*>(u->operand)->mkind == MemberKind::Field)
          return T.void_ptr;  // the address of a void member (a marker): where it is
        t = check_rv(u->operand, nullptr);
        if (u->operand->is_const() && u->operand->cv.kind == ConstValue::TypeV) {
          u->cv = ConstValue::of_type(T.pointer_to(u->operand->cv.t));
          return T.type_t;
        }
        if (t->is_proc() && u->operand->kind == NK::Ident) return t;  // *proc == proc
        if (!is_lvalue(u->operand)) fail(u->loc, "cannot take the address of this expression");
        return T.pointer_to(t);
      }
      case UnOp::Deref: {
        Type* t = check_value(u->operand, nullptr);
        if (!t->is_pointer()) fail(u->loc, "cannot dereference a value of type ", type_name(t));
        if (t->elem->kind == TypeKind::Void) fail(u->loc, "cannot dereference *void");
        complete_type(t->elem, u->loc);
        return t->elem;
      }
      case UnOp::Not: {
        check_cond(u->operand);
        if (u->operand->is_const() && u->operand->cv.kind == ConstValue::Bool)
          u->cv = ConstValue::of_bool(!u->operand->cv.b);
        return T.bool_t;
      }
      case UnOp::Neg:
      case UnOp::Plus: {
        Type* t = check_rv(u->operand, hint);
        if (overloadable(t)) {
          if ((u->overload = operator_call(u->op == UnOp::Neg ? "-" : "+", {u->operand}, u->loc)))
            return u->overload->type;
          if (t->kind == TypeKind::Struct || t->kind == TypeKind::Array)
            fail(u->loc, "no unary 'operator ", u->op == UnOp::Neg ? "-" : "+", "' for ", type_name(t));
        }
        if (!t->is_numeric()) fail(u->loc, "unary '", u->op == UnOp::Neg ? "-" : "+", "' needs a number, not ",
                                   type_name(t));
        if (u->operand->is_const()) {
          u->cv = u->operand->cv;
          if (u->op == UnOp::Neg) {
            if (u->cv.kind == ConstValue::Int) {
              u->cv.i = -u->cv.i;
              if (t->kind == TypeKind::Int) u->cv.i = int_wrap(u->cv.i, t);
            } else {
              u->cv.f = -u->cv.f;
            }
          }
        }
        return t;
      }
      case UnOp::BitNot: {
        Type* t = check_rv(u->operand, hint);
        if (!t->is_integer() && !(t->is_enum() && t->is_flags))
          fail(u->loc, "'~' needs an integer, not ", type_name(t));
        if (t->kind == TypeKind::UntypedInt) {
          // Bitwise-not of an untyped constant needs a width; use the hint or s64.
          Type* to = (hint && hint->kind == TypeKind::Int) ? hint : T.s64;
          coerce(u->operand, to);
          t = to;
        }
        if (u->operand->is_const()) u->cv = ConstValue::of_int(int_wrap(~u->operand->cv.i, t));
        return t;
      }
    }
    return T.void_t;
  }

  void check_binop_types(BinOp op, Type* t, SourceLoc loc) {
    switch (op) {
      case BinOp::Add: case BinOp::Sub: case BinOp::Mul: case BinOp::Div:
        if (!t->is_numeric()) fail(loc, "operator '", binop_str(op), "' is not defined for ", type_name(t));
        return;
      case BinOp::Mod:
        if (!t->is_integer()) fail(loc, "operator '%' needs integers, not ", type_name(t));
        return;
      case BinOp::BitAnd: case BinOp::BitOr: case BinOp::BitXor:
        if (!t->is_integer() && !t->is_enum() && t->kind != TypeKind::Bool)
          fail(loc, "operator '", binop_str(op), "' is not defined for ", type_name(t));
        return;
      case BinOp::Shl: case BinOp::Shr:
        if (!t->is_integer()) fail(loc, "shift needs an integer, not ", type_name(t));
        return;
      case BinOp::Eq: case BinOp::Ne:
        switch (t->kind) {
          case TypeKind::Int: case TypeKind::Float: case TypeKind::Bool: case TypeKind::Pointer:
          case TypeKind::Enum: case TypeKind::String: case TypeKind::Proc: case TypeKind::TypeT:
          case TypeKind::UntypedInt: case TypeKind::UntypedFloat: case TypeKind::Null:
            return;
          default:
            fail(loc, "cannot compare values of type ", type_name(t), " with '", binop_str(op), "'");
        }
      case BinOp::Lt: case BinOp::Le: case BinOp::Gt: case BinOp::Ge:
        if (!t->is_numeric() && !t->is_enum() && !t->is_pointer())
          fail(loc, "cannot order values of type ", type_name(t));
        return;
      default:
        return;
    }
  }

  ConstValue fold(BinOp op, const ConstValue& a, const ConstValue& b, Type* t, SourceLoc loc) {
    auto as_f = [](const ConstValue& c) { return c.kind == ConstValue::Float ? c.f : (double)c.i; };
    bool cmp = op == BinOp::Eq || op == BinOp::Ne || op == BinOp::Lt || op == BinOp::Le || op == BinOp::Gt ||
               op == BinOp::Ge;
    if (a.kind == ConstValue::Bool && b.kind == ConstValue::Bool) {
      switch (op) {
        case BinOp::Eq: return ConstValue::of_bool(a.b == b.b);
        case BinOp::Ne: return ConstValue::of_bool(a.b != b.b);
        case BinOp::LogAnd: case BinOp::BitAnd: return ConstValue::of_bool(a.b && b.b);
        case BinOp::LogOr: case BinOp::BitOr: return ConstValue::of_bool(a.b || b.b);
        case BinOp::BitXor: return ConstValue::of_bool(a.b != b.b);
        default: break;
      }
    }
    if (a.kind == ConstValue::String && b.kind == ConstValue::String) {
      if (op == BinOp::Eq) return ConstValue::of_bool(a.s == b.s);
      if (op == BinOp::Ne) return ConstValue::of_bool(a.s != b.s);
    }
    if (a.kind == ConstValue::TypeV && b.kind == ConstValue::TypeV) {
      if (op == BinOp::Eq) return ConstValue::of_bool(a.t == b.t);
      if (op == BinOp::Ne) return ConstValue::of_bool(a.t != b.t);
    }
    if (a.kind == ConstValue::Null && b.kind == ConstValue::Null) {
      if (op == BinOp::Eq) return ConstValue::of_bool(true);
      if (op == BinOp::Ne) return ConstValue::of_bool(false);
    }
    if (a.kind == ConstValue::Float || b.kind == ConstValue::Float) {
      double x = as_f(a), y = as_f(b);
      switch (op) {
        case BinOp::Add: return ConstValue::of_float(x + y);
        case BinOp::Sub: return ConstValue::of_float(x - y);
        case BinOp::Mul: return ConstValue::of_float(x * y);
        case BinOp::Div: return ConstValue::of_float(x / y);
        case BinOp::Eq: return ConstValue::of_bool(x == y);
        case BinOp::Ne: return ConstValue::of_bool(x != y);
        case BinOp::Lt: return ConstValue::of_bool(x < y);
        case BinOp::Le: return ConstValue::of_bool(x <= y);
        case BinOp::Gt: return ConstValue::of_bool(x > y);
        case BinOp::Ge: return ConstValue::of_bool(x >= y);
        default: return ConstValue{};
      }
    }
    if (a.kind != ConstValue::Int || b.kind != ConstValue::Int) return ConstValue{};
    i128 x = a.i, y = b.i, r = 0;
    if (cmp) {
      switch (op) {
        case BinOp::Eq: return ConstValue::of_bool(x == y);
        case BinOp::Ne: return ConstValue::of_bool(x != y);
        case BinOp::Lt: return ConstValue::of_bool(x < y);
        case BinOp::Le: return ConstValue::of_bool(x <= y);
        case BinOp::Gt: return ConstValue::of_bool(x > y);
        default: return ConstValue::of_bool(x >= y);
      }
    }
    switch (op) {
      case BinOp::Add: r = x + y; break;
      case BinOp::Sub: r = x - y; break;
      case BinOp::Mul: r = x * y; break;
      case BinOp::Div:
        if (y == 0) fail(loc, "division by zero in constant expression");
        r = x / y;
        break;
      case BinOp::Mod:
        if (y == 0) fail(loc, "division by zero in constant expression");
        r = x % y;
        break;
      case BinOp::BitAnd: r = x & y; break;
      case BinOp::BitOr: r = x | y; break;
      case BinOp::BitXor: r = x ^ y; break;
      case BinOp::Shl:
        if (y < 0 || y >= 64) fail(loc, "shift amount out of range in constant expression");
        r = x << (int)y;
        break;
      case BinOp::Shr:
        if (y < 0 || y >= 64) fail(loc, "shift amount out of range in constant expression");
        r = x >> (int)y;
        break;
      default: return ConstValue{};
    }
    if (t && (t->kind == TypeKind::Int || t->kind == TypeKind::Enum)) r = int_wrap(r, t);
    return ConstValue::of_int(r);
  }

  Type* check_binary(Binary* b, Type* hint) {
    if (b->op == BinOp::LogAnd || b->op == BinOp::LogOr) {
      check_cond(b->lhs);
      check_cond(b->rhs);
      if (b->lhs->is_const() && b->rhs->is_const() && b->lhs->cv.kind == ConstValue::Bool &&
          b->rhs->cv.kind == ConstValue::Bool)
        b->cv = fold(b->op, b->lhs->cv, b->rhs->cv, T.bool_t, b->loc);
      return T.bool_t;
    }
    bool arith = b->op == BinOp::Add || b->op == BinOp::Sub || b->op == BinOp::Mul || b->op == BinOp::Div ||
                 b->op == BinOp::Mod || b->op == BinOp::BitAnd || b->op == BinOp::BitOr ||
                 b->op == BinOp::BitXor;
    Type* operand_hint = arith ? hint : nullptr;
    Type *lt, *rt;
    if (is_inferred_member(b->lhs) && !is_inferred_member(b->rhs)) {
      rt = check_rv(b->rhs, operand_hint);
      lt = check_rv(b->lhs, rt->is_untyped() ? operand_hint : rt);
    } else {
      lt = check_rv(b->lhs, operand_hint);
      rt = check_rv(b->rhs, lt->is_untyped() ? operand_hint : lt);
    }

    if (overloadable(lt) || overloadable(rt)) {
      if ((b->overload = operator_call(binop_str(b->op), {b->lhs, b->rhs}, b->loc))) return b->overload->type;
      if (b->op == BinOp::Ne) {  // a != b as !(a == b)
        if (Expr* eq = operator_call("==", {b->lhs, b->rhs}, b->loc)) {
          auto* u = ast_.make<Unary>(b->loc, UnOp::Not, eq);
          Expr* e = u;
          check(e, nullptr);
          b->overload = e;
          return e->type;
        }
      }
      if (lt->kind == TypeKind::Struct || rt->kind == TypeKind::Struct)
        fail(b->loc, "no 'operator ", binop_str(b->op), "' for ", type_name(lt), " and ", type_name(rt));
    }

    // Pointer arithmetic.
    if ((b->op == BinOp::Add || b->op == BinOp::Sub) && lt->is_pointer() && rt->is_integer()) {
      if (lt->elem->kind == TypeKind::Void) fail(b->loc, "arithmetic on *void");
      if (rt->is_untyped()) coerce(b->rhs, T.s64);
      return lt;
    }
    if (b->op == BinOp::Add && rt->is_pointer() && lt->is_integer()) {
      if (lt->is_untyped()) coerce(b->lhs, T.s64);
      std::swap(b->lhs, b->rhs);
      return rt;
    }
    if (b->op == BinOp::Sub && lt->is_pointer() && rt->is_pointer()) {
      if (lt != rt) fail(b->loc, "cannot subtract pointers of different types");
      return T.s64;
    }

    // Shifts: the result has the type of the left operand.
    if (b->op == BinOp::Shl || b->op == BinOp::Shr) {
      if (!lt->is_integer() || !rt->is_integer()) fail(b->loc, "shift needs integer operands");
      if (lt->is_untyped() && rt->is_untyped()) {
        b->cv = fold(b->op, b->lhs->cv, b->rhs->cv, nullptr, b->loc);
        return T.untyped_int;
      }
      if (lt->is_untyped()) {
        coerce(b->lhs, (hint && hint->kind == TypeKind::Int) ? hint : T.s64);
        lt = b->lhs->type;
      }
      if (rt->is_untyped()) {
        if (b->rhs->cv.i < 0 || b->rhs->cv.i >= lt->bits) fail(b->rhs->loc, "shift amount out of range");
        coerce(b->rhs, T.s64);
      }
      if (b->lhs->is_const() && b->rhs->is_const()) b->cv = fold(b->op, b->lhs->cv, b->rhs->cv, lt, b->loc);
      return lt;
    }

    Type* t = nullptr;
    if (lt == rt) {
      t = lt;
    } else if (lt->is_untyped() && rt->is_untyped()) {
      if (lt->is_numeric() && rt->is_numeric()) t = T.untyped_float;  // int mixed with float
      else fail(b->loc, "mismatched constant types");
    } else if (lt->is_untyped()) {
      coerce(b->lhs, rt);
      t = rt;
    } else if (rt->is_untyped()) {
      coerce(b->rhs, lt);
      t = lt;
    } else if (implicit_ok(lt, rt) && rt->kind != TypeKind::Any) {
      coerce(b->lhs, rt);
      t = rt;
    } else if (implicit_ok(rt, lt) && lt->kind != TypeKind::Any) {
      coerce(b->rhs, lt);
      t = lt;
    } else {
      fail(b->loc, "type mismatch: ", type_name(lt), " ", binop_str(b->op), " ", type_name(rt));
    }
    check_binop_types(b->op, t, b->loc);
    if (b->op == BinOp::BitAnd || b->op == BinOp::BitOr || b->op == BinOp::BitXor) {
      if (t->is_enum() && !t->is_flags)
        fail(b->loc, "bitwise operators on enums need enum_flags (", type_name(t), " is a plain enum)");
    }
    bool cmp = b->op == BinOp::Eq || b->op == BinOp::Ne || b->op == BinOp::Lt || b->op == BinOp::Le ||
               b->op == BinOp::Gt || b->op == BinOp::Ge;
    if (b->lhs->is_const() && b->rhs->is_const()) {
      if ((b->op == BinOp::Div || b->op == BinOp::Mod) && b->rhs->cv.kind == ConstValue::Int && b->rhs->cv.i == 0)
        fail(b->loc, "division by zero");
      b->cv = fold(b->op, b->lhs->cv, b->rhs->cv, t, b->loc);
      if (b->cv.kind == ConstValue::Float && t->is_f32()) b->cv.f = (float)b->cv.f;
    } else if ((b->op == BinOp::Div || b->op == BinOp::Mod) && b->rhs->is_const() &&
               b->rhs->cv.kind == ConstValue::Int && b->rhs->cv.i == 0) {
      fail(b->loc, "division by zero");
    }
    if (cmp) return T.bool_t;
    // Integer constant mixed with float constant: fold to float.
    return t;
  }

  Type* check_index(Index* ix) {
    Type* bt = check_value(ix->base, nullptr);
    if (overloadable(bt) && bt->kind == TypeKind::Struct) {
      // `operator *[]` returns a pointer, so v[i] stays assignable; else `operator []`.
      if (Expr* p = operator_call("*[]", {ix->base, ix->index}, ix->loc)) {
        if (!p->type->is_pointer()) fail(ix->loc, "'operator *[]' must return a pointer");
        Expr* d = ast_.make<Unary>(ix->loc, UnOp::Deref, p);
        check(d, nullptr);
        ix->overload = d;
        return d->type;
      }
      if ((ix->overload = operator_call("[]", {ix->base, ix->index}, ix->loc))) return ix->overload->type;
      fail(ix->loc, "cannot index a value of type ", type_name(bt), " (no 'operator []' fits)");
    }
    Type* it = check_value(ix->index, nullptr);
    if (!it->is_integer()) fail(ix->index->loc, "array index must be an integer, not ", type_name(it));
    Type* result = nullptr;
    switch (bt->kind) {
      case TypeKind::Array:
        result = bt->elem;
        if (bt->is_fixed_array() && ix->index->is_const()) {
          i128 v = ix->index->cv.i;
          if (v < 0 || v >= bt->count)
            fail(ix->loc, "array index ", i128_str(v), " is out of bounds (count is ", bt->count, ")");
          // An element of a constant array (e.g. a table made by #run) is a constant.
          if (ix->base->is_const() && ix->base->cv.kind == ConstValue::Aggregate && !ix->base->cv.elems->empty())
            ix->cv = (*ix->base->cv.elems)[(size_t)v];
        }
        break;
      case TypeKind::String: result = T.u8; break;
      case TypeKind::Pointer:
        if (bt->elem->kind == TypeKind::Void) fail(ix->loc, "cannot index *void");
        result = bt->elem;
        break;
      default: fail(ix->loc, "cannot index a value of type ", type_name(bt));
    }
    complete_type(result, ix->loc);
    return result;
  }

  // Names reachable as members of struct t, including those of anonymous members.
  std::vector<std::string> member_names(Type* t) {
    std::vector<std::string> out;
    for (auto& f : t->fields) {
      if (!f.anonymous) out.push_back(f.name);
      if (f.anonymous || f.is_using)
        for (auto& n : member_names(f.type)) out.push_back(n);
    }
    return out;
  }

  // Finds member `name` of struct t, looking inside anonymous members; appends
  // the field indices that lead to it to `path`.
  Type* find_field(Type* t, const std::string& name, std::vector<int>& path) {
    for (size_t i = 0; i < t->fields.size(); ++i) {
      auto& f = t->fields[i];
      path.push_back((int)i);
      if (!f.anonymous && f.name == name) return f.type;
      if (f.anonymous || f.is_using)
        if (Type* ft = find_field(f.type, name, path)) return ft;
      path.pop_back();
    }
    return nullptr;
  }

  Type* check_member(Member* m, Type* hint) {
    if (!m->base) {
      if (!hint || !hint->is_enum()) fail(m->loc, "cannot infer the enum type of '.", m->name, "'");
      return enum_member(m, hint);
    }
    Type* bt = check_rv(m->base, nullptr);
    if (m->base->is_const() && m->base->cv.kind == ConstValue::TypeV) {
      Type* t = m->base->cv.t;
      if (t->is_enum()) return enum_member(m, t);
      fail(m->loc, "type ", type_name(t), " has no constant member '", m->name, "'");
    }
    if (bt->is_untyped()) {
      default_untyped(m->base);
      bt = m->base->type;
    }
    if (bt->kind == TypeKind::TypeT) {  // a Type value: the members of its Type_Info (`a.type.runtime_size`)
      coerce(m->base, T.pointer_to(prog_.type_info.info));
      bt = m->base->type;
    }
    if (bt->is_pointer() && bt->elem->kind != TypeKind::Void) {
      m->through_pointer = true;
      bt = bt->elem;
    }
    switch (bt->kind) {
      case TypeKind::Struct: {
        complete_type(bt, m->loc);
        if (Type* ft = find_field(bt, m->name, m->field_path)) {
          m->mkind = MemberKind::Field;
          if (!m->through_pointer && m->base->is_const() && m->base->cv.kind == ConstValue::Aggregate)
            m->cv = const_field(m->base->cv, bt, m->field_path);
          return ft;
        }
        fail(m->loc, "struct ", type_name(bt), " has no member '", m->name, "'");
      }
      case TypeKind::Array:
        if (m->name == "count") {
          m->mkind = MemberKind::ArrayCount;
          if (bt->is_fixed_array()) m->cv = ConstValue::of_int(bt->count);
          return T.s64;
        }
        if (m->name == "data") {
          m->mkind = MemberKind::ArrayData;
          return T.pointer_to(bt->elem);
        }
        if (m->name == "allocated" && bt->is_dynamic()) {
          m->mkind = MemberKind::ArrayAllocated;
          return T.s64;
        }
        if (m->name == "allocator" && bt->is_dynamic()) {
          m->mkind = MemberKind::ArrayAllocator;
          return allocator_type();
        }
        fail(m->loc, "array type ", type_name(bt), " has no member '", m->name, "'");
      case TypeKind::String:
        if (m->name == "count") {
          m->mkind = MemberKind::StringCount;
          if (m->base->is_const() && m->base->cv.kind == ConstValue::String && !m->through_pointer)
            m->cv = ConstValue::of_int((i128)m->base->cv.s.size());
          return T.s64;
        }
        if (m->name == "data") {
          m->mkind = MemberKind::StringData;
          return T.pointer_to(T.u8);
        }
        fail(m->loc, "string has no member '", m->name, "' (members are count and data)");
      case TypeKind::Any:
        if (m->name == "type") {
          m->mkind = MemberKind::AnyType;
          return T.type_t;
        }
        if (m->name == "value_pointer") {
          m->mkind = MemberKind::AnyValue;
          return T.void_ptr;
        }
        fail(m->loc, "Any has no member '", m->name, "'");
      default:
        fail(m->loc, "type ", type_name(bt), " has no members");
    }
  }

  // A field of a constant struct (through anonymous members), or none for a
  // union member.
  static ConstValue const_field(const ConstValue& v, Type* t, const std::vector<int>& path) {
    ConstValue cur = v;
    for (int i : path) {
      if (t->is_union || cur.kind != ConstValue::Aggregate) return ConstValue{};
      if (cur.elems->empty()) return ConstValue{};  // zero value: left to codegen
      ConstValue next = (*cur.elems)[(size_t)i];  // a copy: assigning it releases cur.elems
      cur = next;
      t = t->fields[(size_t)i].type;
    }
    return cur;
  }

  Type* enum_member(Member* m, Type* et) {
    for (auto& mem : et->members) {
      if (mem.name == m->name) {
        m->mkind = MemberKind::EnumValue;
        m->cv = ConstValue::of_int(mem.value);
        return et;
      }
    }
    fail(m->loc, "enum ", type_name(et), " has no member '", m->name, "'");
  }

  Type* check_cast(Cast* c) {
    Type* to = resolve_type(c->type_expr);
    return do_cast(c, to);
  }

  Type* do_cast(Cast* c, Type* to) {
    c->type = to;
    Type* from = check_rv(c->operand, to->is_numeric() ? nullptr : to);
    complete_type(to, c->loc);
    // Constants convert at compile time.
    if (c->operand->is_const() && from->is_untyped()) {
      const ConstValue& v = c->operand->cv;
      if (to->kind == TypeKind::Int || to->kind == TypeKind::Enum) {
        Type* it = to->kind == TypeKind::Enum ? to->elem : to;
        i128 iv = v.kind == ConstValue::Float ? (i128)v.f : v.i;
        if (v.kind == ConstValue::Null) fail(c->loc, "cannot cast null to ", type_name(to));
        if (!int_fits(iv, it)) {
          if (!c->truncate) fail(c->loc, "constant ", i128_str(iv), " does not fit in ", type_name(to),
                                 " (use cast,trunc to truncate)");
          iv = int_wrap(iv, it);
        }
        c->cv = ConstValue::of_int(iv);
        return to;
      }
      if (to->kind == TypeKind::Float) {
        double f = v.kind == ConstValue::Float ? v.f : (double)v.i;
        c->cv = ConstValue::of_float(to->is_f32() ? (double)(float)f : f);
        return to;
      }
      if (to->kind == TypeKind::Bool && v.kind == ConstValue::Int) {
        c->cv = ConstValue::of_bool(v.i != 0);
        return to;
      }
      if (to->is_pointer() || to->is_proc()) {
        if (v.kind == ConstValue::Null) {
          c->cv = ConstValue::null();
          return to;
        }
        if (v.kind == ConstValue::Int) {  // a fixed address, e.g. a memory-mapped register block
          if (v.i < 0 || v.i > (i128)UINT64_MAX) fail(c->loc, "address ", i128_str(v.i), " is out of range");
          c->cv = ConstValue::address(v.i);
          return to;
        }
      }
      default_untyped(c->operand);
      from = c->operand->type;
    }
    if (from == to) return to;
    bool ok = false;
    auto intlike = [](Type* t) { return t->kind == TypeKind::Int || t->kind == TypeKind::Enum; };
    if ((intlike(from) || from->kind == TypeKind::Float) && (intlike(to) || to->kind == TypeKind::Float)) ok = true;
    else if (from->kind == TypeKind::Bool && intlike(to)) ok = true;
    else if (intlike(from) && to->kind == TypeKind::Bool) ok = true;
    else if ((from->is_pointer() || from->is_proc()) && (to->is_pointer() || to->is_proc())) ok = true;
    else if (from->kind == TypeKind::TypeT && is_type_info_ptr(to)) ok = true;  // cast(*Type_Info_Struct) T
    else if (from->is_pointer() && to->kind == TypeKind::TypeT) ok = true;     // cast(Type) info
    else if ((from->is_pointer() || from->is_proc()) && to->kind == TypeKind::Int && to->bits == 64) ok = true;
    else if (from->kind == TypeKind::Int && (to->is_pointer() || to->is_proc())) ok = true;
    else if (implicit_ok(from, to)) ok = true;
    else if (from->underlying() == to->underlying()) ok = true;  // to or from a distinct type: the same layout
    if (!ok) fail(c->loc, "cannot cast ", type_name(from), " to ", type_name(to));
    // Fold typed numeric constants.
    if (c->operand->is_const()) {
      const ConstValue& v = c->operand->cv;
      if (intlike(to) && (v.kind == ConstValue::Int || v.kind == ConstValue::Float)) {
        Type* it = to->kind == TypeKind::Enum ? to->elem : to;
        i128 iv = v.kind == ConstValue::Float ? (i128)v.f : v.i;
        c->cv = ConstValue::of_int(int_wrap(iv, it));
      } else if (to->kind == TypeKind::Float && (v.kind == ConstValue::Int || v.kind == ConstValue::Float)) {
        double f = v.kind == ConstValue::Float ? v.f : (double)v.i;
        c->cv = ConstValue::of_float(to->is_f32() ? (double)(float)f : f);
      } else if ((to->is_pointer() || to->is_proc()) && (v.kind == ConstValue::Null || v.kind == ConstValue::Address)) {
        c->cv = v;
      } else if ((to->is_pointer() || to->is_proc()) && v.kind == ConstValue::Int && from->kind == TypeKind::Int) {
        c->cv = ConstValue::address(v.i < 0 ? int_wrap(v.i, T.u64) : v.i);
      } else if (to->kind == TypeKind::Int && v.kind == ConstValue::Address) {
        c->cv = ConstValue::of_int(int_wrap(v.i, to));
      }
    }
    return to;
  }

  Type* check_struct_lit(StructLit* s, Type* hint) {
    Type* t = nullptr;
    if (s->type_expr) {
      t = resolve_type(s->type_expr);
    } else {
      if (!hint || hint->kind == TypeKind::Any) fail(s->loc, "cannot infer the type of '.{...}' here");
      t = hint;
    }
    if (t->kind != TypeKind::Struct) fail(s->loc, "struct literal of non-struct type ", type_name(t));
    complete_type(t, s->loc);
    s->field_values.assign(t->fields.size(), nullptr);
    size_t pos = 0;
    bool named_seen = false;
    for (auto& a : s->args) {
      size_t idx;
      if (!a.name.empty()) {
        named_seen = true;
        idx = t->fields.size();
        for (size_t i = 0; i < t->fields.size(); ++i)
          if (t->fields[i].name == a.name) idx = i;
        if (idx == t->fields.size()) fail(a.name_loc, "struct ", type_name(t), " has no member '", a.name, "'");
      } else {
        if (named_seen) fail(a.value->loc, "positional values must come before named ones");
        while (pos < t->fields.size() && t->fields[pos].type->kind == TypeKind::Void) ++pos;  // markers
        idx = pos++;
        if (idx >= t->fields.size()) fail(a.value->loc, "too many values for struct ", type_name(t));
      }
      if (t->fields[idx].type->kind == TypeKind::Void)
        fail(a.name_loc, "member '", a.name, "' of ", type_name(t), " has type void and cannot be set");
      if (s->field_values[idx]) fail(a.value->loc, "member '", t->fields[idx].name, "' is set twice");
      if (t->has_place)
        for (size_t j = 0; j < t->fields.size(); ++j)
          if (j != idx && s->field_values[j] && fields_overlap(t, idx, j))
            fail(a.value->loc, "members '", t->fields[j].name, "' and '", t->fields[idx].name, "' of ", type_name(t),
                 " overlap (#place); a struct literal can only set one of them");
      check_value(a.value, t->fields[idx].type);
      s->field_values[idx] = a.value;
    }
    return t;
  }

  Type* check_array_lit(ArrayLit* a, Type* hint) {
    Type* elem = nullptr;
    if (a->elem_type_expr) {
      elem = resolve_type(a->elem_type_expr);
    } else if (hint && hint->kind == TypeKind::Array) {
      elem = hint->elem;
    }
    if (!elem) {
      if (a->elems.empty()) fail(a->loc, "cannot infer the element type of an empty array literal");
      elem = check_value(a->elems[0], nullptr);
    }
    complete_type(elem, a->loc);
    for (Expr*& x : a->elems) check_value(x, elem);
    Type* t = T.array_of(elem, ArrayKind::Fixed, (int64_t)a->elems.size());
    complete_type(t, a->loc);
    return t;
  }

  Type* check_ifx(Ifx* x, Type* hint) {
    check_cond(x->cond);
    Type* a = check_rv(x->then_e, hint);
    Type* b = check_rv(x->else_e, hint);
    Type* t;
    if (a == b) t = a;
    else if (a->is_untyped() && b->is_untyped()) t = (a->is_float() || b->is_float()) ? T.untyped_float : a;
    else if (a->is_untyped()) t = b;
    else if (b->is_untyped()) t = a;
    else if (implicit_ok(a, b)) t = b;
    else if (implicit_ok(b, a)) t = a;
    else fail(x->loc, "ifx branches have different types: ", type_name(a), " and ", type_name(b));
    if (t->is_untyped()) {
      if (!x->cond->is_const()) {
        // Give the result a concrete type.
        if (hint && !hint->is_untyped() && hint->kind != TypeKind::Any &&
            ((t->kind == TypeKind::UntypedInt && hint->is_numeric()) ||
             (t->kind == TypeKind::UntypedFloat && hint->is_float()) ||
             (t->kind == TypeKind::Null && hint->is_pointer())))
          t = hint;
        else
          t = t->kind == TypeKind::UntypedInt ? T.s64 : t->kind == TypeKind::UntypedFloat ? T.f32 : T.void_ptr;
      }
    }
    if (!t->is_untyped()) {
      coerce(x->then_e, t);
      coerce(x->else_e, t);
    }
    if (x->cond->is_const() && x->cond->cv.kind == ConstValue::Bool) {
      Expr* chosen = x->cond->cv.b ? x->then_e : x->else_e;
      if (chosen->is_const()) x->cv = chosen->cv;
    }
    return t;
  }

  // ------------------------------------------------------------------ polymorphism
  //
  // A polymorphic procedure or struct is a template and is never checked
  // itself. A call infers the bindings of a procedure's type variables (`$T`)
  // and compile-time parameters (`$N: int`, `$T: Type`) from its arguments; a
  // struct instance `Vector(float, 3)` evaluates its parameters. A clone of the
  // template is then checked in a scope that declares the bindings as Type and
  // Const symbols. That scope's parent is the template's declaring scope, so
  // names (module privacy, imports, `using`) resolve as in the template.
  // Instances are cached by template and bindings; a procedure instance is
  // registered for body checking and code generation only once a call uses it.

  struct PolyBinding {
    std::string name;
    SourceLoc loc;
    bool bound = false;
    bool is_value = false;  // a compile-time value ($N: int); otherwise a type
    Type* type = nullptr;   // the bound type, or the type of the value
    ConstValue value;
  };
  using Bindings = std::vector<PolyBinding>;

  struct PolyInstance {
    Symbol* tmpl = nullptr;  // the template's symbol
    std::string desc;        // "T = int, N = 3", for diagnostics
    SourceLoc use;           // the first call that used it
    bool used = false;       // used by a call (in runtime code or only by #run)
  };
  std::map<std::pair<const Node*, std::string>, Symbol*> poly_procs_;  // (template, bindings) -> instance
  std::unordered_map<ProcLit*, PolyInstance> poly_instances_;
  std::map<std::pair<const Node*, std::string>, Type*> poly_structs_;  // (template, bindings) -> instance
  struct PolyStructInstance {
    Symbol* tmpl = nullptr;
    Bindings args;
  };
  std::unordered_map<Type*, PolyStructInstance> poly_struct_types_;

  // How a call's arguments map onto a template's parameters.
  struct ArgMap {
    std::vector<Expr*> params;  // per parameter; null when not given
    std::vector<Expr*> extra;   // arguments of the variadic parameter
    Expr* spread = nullptr;     // `..array` given for the variadic parameter instead
    std::vector<int> param_of;  // per call argument: its parameter
  };

  // The index of p's `..T` parameter, or -1. Parameters before it take the
  // positional arguments, the variadic one the rest; those after it are
  // passed by name.
  static int vararg_index(const ProcLit* p) {
    for (size_t i = 0; i < p->params.size(); ++i)
      if (p->params[i]->is_vararg) return (int)i;
    return -1;
  }

  static bool is_poly_proc_decl(const Symbol* s) {
    return is_proc_decl(s->decl, s->decl_index) && static_cast<ProcLit*>(s->decl->values[s->decl_index])->is_poly;
  }
  static bool is_poly_struct_decl(const Symbol* s) {
    const Decl* d = s->decl;
    if (!d || !d->is_const || d->values.size() != d->names.size()) return false;
    const Expr* v = d->values[s->decl_index];
    return v->kind == NK::StructDef && !static_cast<const StructDef*>(v)->params.empty();
  }
  static StructDef* poly_struct_def(const Symbol* s) { return static_cast<StructDef*>(s->decl->values[s->decl_index]); }

  static int binding_index(const Bindings& b, const std::string& name) {
    for (size_t i = 0; i < b.size(); ++i)
      if (b[i].name == name) return (int)i;
    return -1;
  }

  // Calls f on each `$X` in a type expression (each name, `$X` or `X`, if `all`).
  template <typename F>
  static void for_each_poly(Expr* e, F& f, bool all = false) {
    if (!e) return;
    switch (e->kind) {
      case NK::Ident:
        if (all || static_cast<Ident*>(e)->poly) f(static_cast<Ident*>(e));
        return;
      case NK::PtrType: for_each_poly(static_cast<PtrType*>(e)->elem, f, all); return;
      case NK::ArrType:
        for_each_poly(static_cast<ArrType*>(e)->count, f, all);
        for_each_poly(static_cast<ArrType*>(e)->elem, f, all);
        return;
      case NK::Call:
        for (auto& a : static_cast<Call*>(e)->args) for_each_poly(a.value, f, all);
        return;
      case NK::ProcType:
        for (Expr* x : static_cast<ProcTypeExpr*>(e)->params) for_each_poly(x, f, all);
        for (Expr* x : static_cast<ProcTypeExpr*>(e)->returns) for_each_poly(x, f, all);
        return;
      default:
        return;
    }
  }
  // Whether type expression e names `name` (as `$name` or `name`).
  static bool mentions(Expr* e, const std::string& name) {
    bool found = false;
    auto f = [&](Ident* id) { found |= id->name == name; };
    for_each_poly(e, f, true);
    return found;
  }
  static Ident* find_poly(Expr* e) {
    Ident* found = nullptr;
    auto f = [&](Ident* id) { if (!found) found = id; };
    for_each_poly(e, f);
    return found;
  }

  // A parameter type as written, for diagnostics: `[]$T`, `Vec(T)`.
  static std::string pattern_str(const Expr* e) {
    if (!e) return "";
    switch (e->kind) {
      case NK::Ident: {
        auto* id = static_cast<const Ident*>(e);
        return (id->poly ? "$" : "") + id->name;
      }
      case NK::IntLit: return i128_str((i128) static_cast<const IntLit*>(e)->value);
      case NK::Member: {
        auto* m = static_cast<const Member*>(e);
        return (m->base ? pattern_str(m->base) : "") + "." + m->name;
      }
      case NK::PtrType: return "*" + pattern_str(static_cast<const PtrType*>(e)->elem);
      case NK::ArrType: {
        auto* a = static_cast<const ArrType*>(e);
        std::string pre = a->akind == ArrayKind::View      ? "[]"
                          : a->akind == ArrayKind::Dynamic ? "[..]"
                                                           : "[" + pattern_str(a->count) + "]";
        return pre + pattern_str(a->elem);
      }
      case NK::Call: {
        auto* c = static_cast<const Call*>(e);
        std::string s = pattern_str(c->callee) + "(";
        for (size_t i = 0; i < c->args.size(); ++i) s += (i ? ", " : "") + pattern_str(c->args[i].value);
        return s + ")";
      }
      case NK::ProcType: {
        auto* p = static_cast<const ProcTypeExpr*>(e);
        std::string s = "(";
        for (size_t i = 0; i < p->params.size(); ++i) s += (i ? ", " : "") + pattern_str(p->params[i]);
        s += ")";
        for (size_t i = 0; i < p->returns.size(); ++i) s += (i ? ", " : " -> ") + pattern_str(p->returns[i]);
        return s;
      }
      default:
        return "...";
    }
  }
  static std::string proc_pattern_str(const ProcLit* p) {
    std::string s = "(";
    for (size_t i = 0; i < p->params.size(); ++i) {
      const Decl* d = p->params[i];
      s += (i ? ", " : "") + std::string(d->is_baked ? "$" : "") + d->names[0].name;
      if (d->type_expr) s += ": " + std::string(d->is_vararg ? ".." : "") + pattern_str(d->type_expr);
    }
    s += ")";
    for (size_t i = 0; i < p->returns.size(); ++i) s += (i ? ", " : " -> ") + pattern_str(p->returns[i].type_expr);
    return s;
  }

  static std::string const_key(const ConstValue& v) {
    switch (v.kind) {
      case ConstValue::Int: case ConstValue::Address: return i128_str(v.i);
      case ConstValue::Float: {
        char buf[48];
        snprintf(buf, sizeof buf, "%a", v.f);
        return buf;
      }
      case ConstValue::Bool: return v.b ? "true" : "false";
      case ConstValue::String: return std::to_string(v.s.size()) + ":" + v.s;
      case ConstValue::TypeV: return "type " + std::to_string((uintptr_t)v.t);
      case ConstValue::Null: return "null";
      case ConstValue::Code: return "code " + i128_str(v.i);
      case ConstValue::Aggregate: {
        std::string k = "{";
        for (const ConstValue& e : *v.elems) k += const_key(e) + ",";
        return k + "}";
      }
      default: return "?";
    }
  }
  static std::string binding_key(const Bindings& b) {
    std::string k;
    for (auto& x : b) k += std::to_string((uintptr_t)x.type) + (x.is_value ? "=" + const_key(x.value) : "") + ";";
    return k;
  }
  static std::string value_str(const PolyBinding& x) {
    if (!x.is_value) return type_name(x.type);
    const ConstValue& v = x.value;
    switch (v.kind) {
      case ConstValue::Int: return i128_str(v.i);
      case ConstValue::Float: {
        std::ostringstream os;
        os << v.f;
        return os.str();
      }
      case ConstValue::Bool: return v.b ? "true" : "false";
      case ConstValue::String: return "\"" + v.s + "\"";
      case ConstValue::Aggregate: return "{...}";
      case ConstValue::Code: return "(Code)";
      default: return const_key(v);
    }
  }
  static std::string bindings_desc(const Bindings& b) {
    std::string s;
    for (auto& x : b) s += (s.empty() ? "" : ", ") + x.name + " = " + value_str(x);
    return s;
  }

  // A scope declaring the bound variables of `b`, inside `parent`.
  Scope* bind_scope(Scope* parent, const Bindings& b) {
    Scope* sc = new_scope(parent, parent->proc);
    for (auto& x : b) {
      if (!x.bound) continue;
      Symbol* s = declare(sc, x.name, x.is_value ? SymKind::Const : SymKind::Type, x.loc);
      s->type = x.is_value ? x.type : T.type_t;
      s->cv = x.is_value ? x.value : ConstValue::of_type(x.type);
      s->state = Symbol::State::Resolved;
    }
    return sc;
  }

  // Runs f with the template scope `tscope` plus the bindings `b` as the
  // current scope. (On an error the context is left as it is; callers that
  // catch errors restore their own saved context.)
  template <typename F>
  auto with_bindings(Scope* tscope, const Bindings& b, F f) {
    Context c = save();
    scope_ = bind_scope(tscope, b);
    proc_ = tscope->proc;
    loops_.clear();
    defer_depth_ = 0;
    macros_.clear();
    auto r = f();
    restore(c);
    return r;
  }

  // A parameter type of a template, with the bindings made so far. The type
  // expression is cloned: checking caches results in the tree.
  Type* param_type_in(Symbol* tmpl, const Bindings& b, Expr* type_expr) {
    return with_bindings(tmpl->scope, b, [&] {
      Expr* e = clone_expr(ast_, type_expr);
      return resolve_type(e);
    });
  }

  // The type of compile-time parameter `d`: declared (`$N: int`) or that of
  // its default value (`$N := 10`).
  Type* baked_param_type(Symbol* tmpl, const Bindings& b, Decl* d) {
    if (d->type_expr) return param_type_in(tmpl, b, d->type_expr);
    return with_bindings(tmpl->scope, b, [&] {
      Expr* v = clone_expr(ast_, d->values[0]);
      return check_value(v, nullptr);
    });
  }

  void validate_poly_proc(ProcLit* p) {
    for (auto& r : p->returns)
      if (Ident* id = find_poly(r.type_expr))
        fail(id->loc, "'$", id->name, "' in a return type is not supported yet; introduce type variables in the ",
             "parameters and write plain '", id->name, "' in the return type");
    if (p->is_foreign) fail(p->loc, "a #foreign procedure cannot be polymorphic");
    for (Decl* d : p->params) {
      if (!d->is_baked) continue;
      const std::string& n = d->names[0].name;
      if (d->is_vararg) fail(d->loc, "compile-time parameter '$", n, "' cannot be variadic");
      if (d->is_using) fail(d->loc, "'using' is not allowed on compile-time parameter '$", n, "'");
    }
    poly_vars(p);  // reports a variable introduced twice
  }

  void validate_poly_struct(StructDef* sd, const std::string& name) {
    std::unordered_set<std::string> seen;
    for (Decl* d : sd->params) {
      if (!seen.insert(d->names[0].name).second)
        fail(d->loc, "duplicate parameter '", d->names[0].name, "' in polymorphic struct '", name, "'");
      if (Ident* id = find_poly(d->type_expr))
        fail(id->loc, "'$", id->name, "' is not allowed in the type of a struct parameter");
    }
  }

  // The type variables and compile-time parameters of a polymorphic
  // procedure, unbound, in order of appearance.
  Bindings poly_vars(ProcLit* p) {
    Bindings b;
    auto add = [&](const std::string& n, SourceLoc loc) {
      if (binding_index(b, n) >= 0)
        fail(loc, "'$", n, "' is introduced twice in '", p->name, "'; write later uses as plain '", n, "'");
      PolyBinding x;
      x.name = n;
      x.loc = loc;
      b.push_back(x);
    };
    auto visit = [&](Ident* id) { add(id->name, id->loc); };
    for (Decl* d : p->params) {
      if (d->is_baked) add(d->names[0].name, d->loc);
      for_each_poly(d->type_expr, visit);
    }
    return b;
  }

  // Binds compile-time parameter `x` (declared with type `pt`) to argument `a`.
  void bind_param(PolyBinding& x, Expr*& a, Type* pt, const std::string& owner) {
    check_rv(a, pt == T.type_t ? nullptr : pt);
    if (pt == T.type_t) {
      if (!a->is_const() || a->cv.kind != ConstValue::TypeV)
        fail(a->loc, "parameter '", x.name, "' of '", owner, "' needs a type, not a value of type ", type_name(a->type));
      x.type = a->cv.t;
    } else {
      if (a->is_const() && a->cv.kind == ConstValue::TypeV)
        fail(a->loc, "parameter '", x.name, "' of '", owner, "' needs a value of type ", type_name(pt), ", not a type");
      if (!a->is_const())
        fail(a->loc, "parameter '", x.name, "' of '", owner, "' is a compile-time parameter; its argument must be a constant");
      if (arg_cost(a, pt) < 0)
        fail(a->loc, "parameter '", x.name, "' of '", owner, "' needs a ", type_name(pt), ", not ", type_name(a->type));
      x.is_value = true;
      x.type = pt;
      x.value = a->cv;
      if (pt->kind == TypeKind::Float && x.value.kind == ConstValue::Int) x.value = ConstValue::of_float((double)x.value.i);
      if (pt->is_f32() && x.value.kind == ConstValue::Float) x.value.f = (float)x.value.f;
    }
    x.bound = true;
  }

  ArgMap map_args(const std::vector<Arg>& args, ProcLit* p, SourceLoc loc) {
    size_t n = p->params.size();
    int va = vararg_index(p);
    size_t fixed = va >= 0 ? (size_t)va : n;
    ArgMap m;
    m.params.assign(n, nullptr);
    size_t pos = 0;
    bool named_seen = false;
    for (auto& a : args) {
      if (!a.name.empty()) {
        named_seen = true;
        size_t idx = n;
        for (size_t i = 0; i < n; ++i)
          if (p->params[i]->names[0].name == a.name) idx = i;
        if (idx == n) fail(a.name_loc, "procedure '", p->name, "' has no parameter named '", a.name, "'");
        if (m.params[idx]) fail(a.name_loc, "parameter '", a.name, "' is given twice");
        if ((int)idx == va) fail(a.name_loc, "cannot pass a variadic parameter by name");
        m.params[idx] = a.value;
        m.param_of.push_back((int)idx);
        continue;
      }
      if (named_seen) fail(a.value->loc, "positional arguments must come before named ones");
      if (a.spread) {
        check_spread(a, pos, fixed, va >= 0, m.extra.size());
        m.spread = a.value;
        m.param_of.push_back(va);
        continue;
      }
      if (pos < fixed) {
        m.param_of.push_back((int)pos);
        m.params[pos++] = a.value;
      } else if (va >= 0) {
        if (m.spread) fail_after_spread(a.value);
        m.extra.push_back(a.value);
        m.param_of.push_back(va);
      } else {
        fail(a.value->loc, "too many arguments for '", p->name, "': expected ", n);
      }
    }
    return m;
  }

  // A call's arguments without those of compile-time parameters (they are
  // baked into the instance).
  std::vector<Arg> runtime_args(const std::vector<Arg>& args, ProcLit* tmpl, SourceLoc loc) {
    ArgMap m = map_args(args, tmpl, loc);
    std::vector<Arg> out;
    for (size_t i = 0; i < args.size(); ++i)
      if (!tmpl->params[m.param_of[i]]->is_baked) out.push_back(args[i]);
    return out;
  }

  // The polymorphic struct a type pattern's callee names (`Vec` in `Vec($T)`), if any.
  Symbol* poly_struct_named(Expr* callee) {
    Symbol* s = nullptr;
    if (callee->kind == NK::Ident) {
      s = find(static_cast<Ident*>(callee)->name, callee->loc).sym;
    } else if (callee->kind == NK::Member && static_cast<Member*>(callee)->base) {
      auto* m = static_cast<Member*>(callee);
      if (Scope* ms = module_of(m->base)) {
        auto it = ms->syms.find(m->name);
        if (it != ms->syms.end()) s = exported_sym(it->second);
      }
    }
    return s && is_poly_struct_decl(s) ? s : nullptr;
  }

  // Matches parameter type pattern `pat` against the type `t` (part of the type
  // of argument `arg`), binding each `$X` in it.
  void match_type(Symbol* tmpl, Expr* pat, Type* t, Expr* arg, const std::string& pname, Bindings& b) {
    auto mismatch = [&] {
      if (find_poly(pat))
        fail(arg->loc, "argument for '", pname, "' has type ", type_name(arg->type), ", which does not match ",
             pattern_str(pat), " in '", tmpl->name, "'");
    };
    auto bind_value = [&](Expr* e, const PolyBinding& v) {  // `$N` in a value position
      if (e->kind != NK::Ident || !static_cast<Ident*>(e)->poly) return;
      PolyBinding& x = b[binding_index(b, static_cast<Ident*>(e)->name)];
      x.bound = true;
      x.is_value = true;
      x.type = v.type;
      x.value = v.value;
    };
    switch (pat->kind) {
      case NK::Ident: {
        auto* id = static_cast<Ident*>(pat);
        if (!id->poly) return;  // a plain name: checked against the instance's parameter type
        PolyBinding& x = b[binding_index(b, id->name)];
        x.bound = true;
        x.type = t;
        return;
      }
      case NK::PtrType:
        if (!t->is_pointer()) return mismatch();
        return match_type(tmpl, static_cast<PtrType*>(pat)->elem, t->elem, arg, pname, b);
      case NK::ArrType: {
        auto* a = static_cast<ArrType*>(pat);
        // []$T also matches fixed and dynamic arrays, which convert to views.
        if (!t->is_array() || (a->akind == ArrayKind::Fixed && !t->is_fixed_array()) ||
            (a->akind == ArrayKind::Dynamic && !t->is_dynamic()))
          return mismatch();
        if (a->akind == ArrayKind::Fixed) {
          PolyBinding count;
          count.type = T.s64;
          count.value = ConstValue::of_int(t->count);
          bind_value(a->count, count);
        }
        return match_type(tmpl, a->elem, t->elem, arg, pname, b);
      }
      case NK::Call: {  // Vec($T): an instance of a polymorphic struct
        auto* c = static_cast<Call*>(pat);
        Scope* saved = scope_;
        scope_ = tmpl->scope;
        Symbol* ps = poly_struct_named(c->callee);
        scope_ = saved;
        if (!ps) return;
        auto it = poly_struct_types_.find(t);
        if (it == poly_struct_types_.end() || it->second.tmpl != ps) return mismatch();
        StructDef* sd = poly_struct_def(ps);
        size_t pos = 0;
        for (auto& a : c->args) {
          size_t idx = pos++;
          if (!a.name.empty())
            for (idx = 0; idx < sd->params.size() && sd->params[idx]->names[0].name != a.name; ++idx) {}
          if (idx >= sd->params.size()) return;
          const PolyBinding& v = it->second.args[idx];
          if (v.is_value) bind_value(a.value, v);
          else match_type(tmpl, a.value, v.type, arg, pname, b);
        }
        return;
      }
      case NK::ProcType: {
        auto* p = static_cast<ProcTypeExpr*>(pat);
        if (!t->is_proc() || p->params.size() != t->params.size() || p->returns.size() != t->returns.size())
          return mismatch();
        for (size_t i = 0; i < p->params.size(); ++i) match_type(tmpl, p->params[i], t->params[i], arg, pname, b);
        for (size_t i = 0; i < p->returns.size(); ++i) match_type(tmpl, p->returns[i], t->returns[i], arg, pname, b);
        return;
      }
      default:
        return;
    }
  }

  // Infers the bindings of polymorphic procedure `tmpl` from arguments that
  // are checked (except hint-dependent ones such as `.{...}`).
  Bindings infer_poly(Symbol* tmpl, ArgMap& m, SourceLoc loc) {
    ProcLit* p = tmpl->proc;
    Bindings b = poly_vars(p);
    std::vector<std::pair<int, Type*>> untyped;  // `$T` given an untyped constant
    struct Lambda {
      ProcTypeExpr* pat;
      Expr* arg;
      std::string pname;
    };
    std::vector<Lambda> lambdas;  // short lambdas with untyped parameters, for procedure type patterns
    auto match_arg = [&](Expr* pat, Expr* a, const std::string& pname) {
      if (!a->type && a->kind == NK::ProcLit && pat->kind == NK::ProcType && needs_hint(a)) {
        lambdas.push_back({static_cast<ProcTypeExpr*>(pat), a, pname});
        return;
      }
      if (!a->type) return;  // .{...}, .[...], .NAME, xx: typed by the instance's parameter
      if (a->type->is_untyped()) {  // `$T` or a later plain `T`
        int k = pat->kind == NK::Ident ? binding_index(b, static_cast<Ident*>(pat)->name) : -1;
        if (k >= 0) untyped.push_back({k, a->type});
        return;
      }
      match_type(tmpl, pat, a->type, a, pname, b);
    };
    for (size_t i = 0; i < p->params.size(); ++i) {
      Decl* d = p->params[i];
      const std::string& pname = d->names[0].name;
      if (d->is_baked) {
        PolyBinding& x = b[binding_index(b, pname)];
        Type* pt = baked_param_type(tmpl, b, d);
        if (m.params[i]) {
          bind_param(x, m.params[i], pt, p->name);
        } else if (!d->values.empty()) {
          with_bindings(tmpl->scope, b, [&] {
            Expr* v = clone_expr(ast_, d->values[0]);
            bind_param(x, v, pt, p->name);
            return 0;
          });
        } else {
          fail(loc, "missing argument for compile-time parameter '$", pname, "' of '", p->name, "'");
        }
        continue;
      }
      if (!d->type_expr) continue;
      if (d->is_vararg) {
        for (Expr* a : m.extra) match_arg(d->type_expr, a, pname);
        if (m.spread && m.spread->type) {
          Type* st = m.spread->type;
          if (st->kind != TypeKind::Array)
            fail(m.spread->loc, "'..' needs an array, not ", type_name(st));
          match_type(tmpl, d->type_expr, st->elem, m.spread, pname, b);
        }
      } else if (m.params[i]) {
        match_arg(d->type_expr, m.params[i], pname);
      }
    }
    // A variable only given untyped constants gets their default type, as in
    // `x := 1;` (float if any of them is a float).
    for (auto& [k, t] : untyped) {
      if (b[k].bound || t->kind == TypeKind::Null) continue;
      bool any_float = false;
      for (auto& [k2, t2] : untyped) any_float |= k2 == k && t2->kind == TypeKind::UntypedFloat;
      b[k].bound = true;
      b[k].type = any_float ? T.f32 : T.s64;
    }
    // `map(xs, (x) => x * 2)` for `f: (T) -> $R`: the lambda's parameters get
    // the types the pattern has with the bindings so far, then its results
    // (the type of its expression) bind what the pattern's results introduce.
    for (Lambda& l : lambdas) {
      std::vector<Type*> ps;
      for (Expr* x : l.pat->params) {
        for (auto& v : b)
          if (!v.bound && mentions(x, v.name))
            fail(l.arg->loc, "cannot infer the parameter types of this lambda for '", l.pname, "' of '", p->name,
                 "' ('$", v.name, "' is not known from the other arguments); give its parameters types");
        ps.push_back(param_type_in(tmpl, b, x));
      }
      lambda_hint_ = T.proc(ps, {}, false, -1);
      lambda_hint_results_ = false;
      Expr* a = l.arg;
      check(a, lambda_hint_);
      match_type(tmpl, l.pat, a->type, a, l.pname, b);
    }
    for (auto& x : b)
      if (!x.bound)
        fail(loc, "cannot infer '$", x.name, "' of '", p->name, "' from the arguments (", p->name, " :: ",
             proc_pattern_str(p), ")");
    run_modify(tmpl, b, loc);
    return b;
  }

  // The instance of polymorphic procedure `tmpl` for bindings `b`, with its
  // signature checked. It is not registered until use_instance.
  Symbol* instantiate_proc(Symbol* tmpl, const Bindings& b, SourceLoc use) {
    auto key = std::make_pair((const Node*)tmpl->proc, binding_key(b));
    auto it = poly_procs_.find(key);
    if (it != poly_procs_.end()) return it->second;
    auto* p = static_cast<ProcLit*>(clone_expr(ast_, tmpl->proc));
    p->is_poly = false;
    std::vector<Decl*> params;
    for (Decl* d : p->params)
      if (!d->is_baked) params.push_back(d);
    p->params = params;
    Symbol* s = new_symbol(tmpl->name, SymKind::Proc, tmpl->loc);
    s->scope = tmpl->scope;
    s->is_global = tmpl->is_global;
    s->proc = p;
    s->state = Symbol::State::Resolved;
    poly_instances_[p] = PolyInstance{tmpl, bindings_desc(b), use, false};
    poly_procs_[key] = s;  // before the signature: it may mention the procedure itself
    try {
      with_bindings(tmpl->scope, b, [&] {
        define_proc(p, /*for_codegen=*/false);
        return 0;
      });
    } catch (const CompileError& e) {
      poly_procs_.erase(key);
      throw CompileError(e.loc, cat(e.what(), "\n    in the signature of polymorphic procedure '", tmpl->name,
                                    "' with ", bindings_desc(b)));
    }
    s->type = p->type;
    return s;
  }

  void use_instance(Symbol* s, SourceLoc use) {
    PolyInstance& in = poly_instances_.at(s->proc);
    if (!in.used) {
      in.used = true;
      in.use = use;
    }
    want_proc(s->proc);
  }

  // Instantiates polymorphic procedure `tmpl` for call `c`; `cost` is how well
  // the arguments fit the instance (as call_cost). Throws if they do not fit.
  Symbol* poly_for_call(Call* c, Symbol* tmpl, int& cost) {
    ProcLit* p = tmpl->proc;
    ArgMap m = map_args(c->args, p, c->loc);
    Bindings b = infer_poly(tmpl, m, c->loc);
    Symbol* inst = instantiate_proc(tmpl, b, c->loc);
    size_t j = 0;
    for (size_t i = 0; i < p->params.size(); ++i) {
      Decl* d = p->params[i];
      if (d->is_baked) continue;
      Type* pt = inst->type->params[j++];
      auto fits = [&](Expr* a, Type* want) {
        if (arg_cost(a, want) < 0)
          fail(a->loc, "argument for '", d->names[0].name, "' has type ", a->type ? type_name(a->type) : "?", ", but '",
               p->name, "' with ", bindings_desc(b), " expects ", type_name(want));
      };
      if (d->is_vararg) {
        for (Expr* a : m.extra) fits(a, pt->elem);
        if (m.spread) fits(m.spread, pt);
      } else if (m.params[i]) {
        fits(m.params[i], pt);
      }
    }
    Call runtime(c->loc, c->callee);
    runtime.args = runtime_args(c->args, p, c->loc);
    cost = call_cost(&runtime, inst->type, inst->proc);
    if (cost < 0) fail(c->loc, "the arguments do not fit '", p->name, "' with ", bindings_desc(b));
    return inst;
  }

  // The instance of polymorphic struct `tmpl` for the parameters `args`
  // (checked in the current scope).
  Type* instantiate_struct(Symbol* tmpl, std::vector<Arg>& args, SourceLoc loc) {
    StructDef* sd = poly_struct_def(tmpl);
    size_t n = sd->params.size();
    std::string list;
    for (Decl* d : sd->params) list += (list.empty() ? "" : ", ") + d->names[0].name;
    auto count_error = [&](SourceLoc l) {
      fail(l, "polymorphic struct '", tmpl->name, "' takes ", n, " parameter", n == 1 ? "" : "s", " (", list, "), but ",
           args.size(), args.size() == 1 ? " was" : " were", " given");
    };
    std::vector<Expr**> given(n, nullptr);
    size_t pos = 0;
    for (auto& a : args) {
      if (a.spread) fail(a.value->loc, "'..' is not allowed in the parameters of polymorphic struct '", tmpl->name, "'");
      size_t idx = pos;
      if (!a.name.empty()) {
        for (idx = 0; idx < n && sd->params[idx]->names[0].name != a.name; ++idx) {}
        if (idx == n) fail(a.name_loc, "polymorphic struct '", tmpl->name, "' has no parameter named '", a.name, "'");
      } else if (pos++ >= n) {
        count_error(a.value->loc);
      }
      if (given[idx]) fail(a.value->loc, "parameter '", sd->params[idx]->names[0].name, "' is given twice");
      given[idx] = &a.value;
    }
    Bindings b;
    for (size_t j = 0; j < n; ++j) {
      Decl* d = sd->params[j];
      PolyBinding x;
      x.name = d->names[0].name;
      x.loc = d->loc;
      Type* pt = baked_param_type(tmpl, b, d);
      if (given[j]) {
        bind_param(x, *given[j], pt, tmpl->name);
      } else if (!d->values.empty()) {
        with_bindings(tmpl->scope, b, [&] {
          Expr* v = clone_expr(ast_, d->values[0]);
          bind_param(x, v, pt, tmpl->name);
          return 0;
        });
      } else {
        count_error(loc);
      }
      b.push_back(x);
    }
    auto key = std::make_pair((const Node*)sd, binding_key(b));
    auto it = poly_structs_.find(key);
    if (it != poly_structs_.end()) return it->second;
    std::string name = tmpl->name + "(";
    for (size_t j = 0; j < n; ++j) name += (j ? ", " : "") + value_str(b[j]);
    name += ")";
    auto* inst = static_cast<StructDef*>(clone_expr(ast_, sd));
    inst->params.clear();
    Type* t = with_bindings(tmpl->scope, b, [&] { return define_struct(inst, name); });
    poly_structs_[key] = t;
    poly_struct_types_[t] = PolyStructInstance{tmpl, b};
    return t;
  }

  // ------------------------------------------------------------------ metaprogramming
  //
  // `#code` makes a Code constant: an id into codes_, which keeps the syntax
  // unchecked. `#insert` checks a fresh copy of it (or parses a string) where
  // it is used, rewriting the tree in place, so code generation and the #run
  // interpreter only ever see ordinary checked nodes.
  //
  // A call of an #expand macro becomes a MacroExpansion: the parameters as
  // locals of the caller, then a copy of the macro body, checked in a scope
  // whose parent is the macro's own scope (names in the body mean what they
  // mean where the macro is declared). Backtick names reach the caller's
  // scope (MacroCtx::caller_scope).

  // Registers inserted text as a source file, so errors in it point into it.
  uint32_t insert_source(const std::string& text, SourceLoc at) {
    return sm_.add(cat("<inserted at ", sm_.get(at.file).path, ":", at.line, ">"), text, at);
  }

  // The constant an #insert operand evaluates to: Code or a string.
  ConstValue insert_operand(Expr*& op) {
    Type* t = check_value(op, nullptr);
    if (t->kind != TypeKind::Code && t->kind != TypeKind::String)
      fail(op->loc, "#insert needs a Code or string constant, not a value of type ", type_name(t));
    if (!op->is_const())
      fail(op->loc, "#insert needs a compile-time constant (a #code value, or a string made with #run)");
    return op->cv;
  }

  const CodeEntry& code_entry(const ConstValue& v) {
    if (v.kind != ConstValue::Code || v.i < 1 || (size_t)v.i > codes_.size())
      fail(SourceLoc{}, "internal error: invalid Code value");
    return codes_[(size_t)v.i - 1];
  }

  // Runs f in the context where Code `c` is checked. That is the insertion
  // point, except for code inserted by a macro that its caller wrote (a Code
  // argument): that code means what it means at the call, so it is checked
  // in a new scope inside the one it was written in, with the caller's macro
  // context (its `return` and backtick names are the caller's).
  template <typename F>
  void in_code_context(const CodeEntry& c, F f) {
    bool on_chain = false;
    for (Scope* s = scope_; s && !on_chain; s = s->parent) on_chain = s == c.scope;
    int current = macros_.empty() ? 0 : macros_.back().id;
    if (!(on_chain && c.macro_id == current) && c.scope && !c.scope->is_module && c.scope != universe_ &&
        c.scope->proc == proc_) {
      size_t keep = 0;
      bool found = c.macro_id == 0;
      for (size_t k = 0; k < macros_.size() && !found; ++k)
        if (macros_[k].id == c.macro_id) {
          keep = k + 1;
          found = true;
        }
      if (found && keep < macros_.size()) {
        // Backtick declarations the macros being left made in the blocks
        // around this point (innermost first) are visible to the code.
        std::vector<Symbol*> visible;
        for (auto it = nested_backticks_.rbegin(); it != nested_backticks_.rend(); ++it) {
          bool left = false;
          for (size_t k = keep; k < macros_.size(); ++k) left |= macros_[k].id == it->first;
          if (!left) continue;
          for (Scope* sc = scope_; sc; sc = sc->parent)
            if (sc == it->second->scope) {
              visible.push_back(it->second);
              break;
            }
        }
        ContextGuard guard(*this);
        in_run_operand_ = macros_[keep].caller_in_run_operand;
        macros_.resize(keep);
        scope_ = new_scope(c.scope, proc_);
        for (Symbol* v : visible) scope_->syms.emplace(v->name, v);  // keeps an inner one of the same name
        f();
        return;
      }
    }
    f();
  }

  // `#insert x;` as a statement: the inserted statements are checked in the
  // current scope (an inserted declaration is visible after the #insert).
  void check_insert(Insert* ins) {
    ConstValue v = insert_operand(ins->operand);
    auto* blk = ast_.make<Block>(ins->loc);
    blk->scope = scope_;
    ins->result = blk;
    with_note(insert_note(ins->loc), [&] {
      if (v.kind == ConstValue::String) {
        if (ins->remove_code) fail(ins->loc, "#insert (remove = ...) needs a Code value, not a string");
        blk->stmts = parse_source_stmts(ast_, sm_, insert_source(v.s, ins->loc), false);
        for (Stmt* x : blk->stmts) check_stmt(x);
        return 0;
      }
      const CodeEntry& c = code_entry(v);
      if (c.block) blk->stmts = static_cast<Block*>(clone_stmt(ast_, c.block))->stmts;
      else blk->stmts.push_back(ast_.make<ExprStmt>(c.expr->loc, clone_expr(ast_, c.expr)));
      if (ins->remove_code) insert_removes_.push_back({ins->remove_code, save(), in_run_operand_});
      in_code_context(c, [&] {
        for (Stmt* x : blk->stmts) check_stmt(x);
      });
      if (ins->remove_code) insert_removes_.pop_back();
      return 0;
    });
  }

  // `#insert x` as an expression: the expression is replaced by the inserted one.
  Type* check_insert_expr(Expr*& e, Type* hint) {
    auto* d = static_cast<DirectiveExpr*>(e);
    ConstValue v = insert_operand(d->operand);
    SourceLoc at = d->loc;
    return with_note(insert_note(at), [&] {
      Expr* x;
      if (v.kind == ConstValue::String) {
        x = parse_source_expr(ast_, sm_, insert_source(v.s, at));
        e = x;
        return check(e, hint);
      }
      const CodeEntry& c = code_entry(v);
      if (!c.expr) fail(at, "a #code block cannot be inserted as an expression (insert it as a statement)");
      x = clone_expr(ast_, c.expr);
      e = x;
      Type* t = nullptr;
      in_code_context(c, [&] { t = check(e, hint); });
      return t;
    });
  }

  // The members a struct body's `#insert x;` makes.
  std::vector<Decl*> inserted_members(Decl* d) {
    ConstValue v = insert_operand(d->insert);
    if (v.kind == ConstValue::String) return parse_source_members(ast_, sm_, insert_source(v.s, d->loc));
    const CodeEntry& c = code_entry(v);
    if (!c.block) fail(c.loc, "only a #code block of member declarations can be inserted into a struct");
    std::vector<Decl*> out;
    for (Stmt* x : static_cast<Block*>(clone_stmt(ast_, c.block))->stmts) {
      if (x->kind != NK::Decl) fail(x->loc, "only member declarations can be inserted into a struct");
      out.push_back(static_cast<Decl*>(x));
    }
    return out;
  }

  // The innermost macro expansion, for a construct (`what`) that must be at
  // the top level of a macro body.
  const MacroCtx& top_level_macro(SourceLoc loc, const char* what) {
    if (macros_.empty()) fail(loc, what, " is only allowed in the body of an #expand macro");
    if (scope_ != macros_.back().body_scope) fail(loc, what, " must be at the top level of the macro body");
    return macros_.back();
  }

  // `name: a name of the scope the macro was called from.
  Type* check_backtick_name(Expr*& e, Type* hint) {
    auto* id = static_cast<Ident*>(e);
    if (macros_.empty())
      fail(id->loc, "backtick names (`", id->name, ") are only allowed in the body of an #expand macro");
    MacroCtx m = macros_.back();
    // In a for_expansion for `for x, i: ...`, `it and `it_index are x and i.
    if (id->name == "it" && !m.rename_it.empty()) id->name = m.rename_it;
    else if (id->name == "it_index" && !m.rename_index.empty()) id->name = m.rename_index;
    // A backtick declaration of this macro in an enclosing block (`it := ...; ... `it).
    for (auto it = nested_backticks_.rbegin(); it != nested_backticks_.rend(); ++it) {
      if (it->first != m.id || it->second->name != id->name) continue;
      for (Scope* sc = scope_; sc; sc = sc->parent)
        if (sc == it->second->scope) {
          id->sym = it->second;
          return check_name(e, hint);
        }
    }
    ContextGuard guard(*this);
    macros_.pop_back();
    scope_ = m.caller_scope;
    in_run_operand_ = m.caller_in_run_operand;
    Found f = find(id->name, id->loc);
    if (!f.sym && !f.via && !f.enum_type)
      fail(id->loc, "'`", id->name, "': '", id->name, "' is not declared where macro '", m.exp->macro->name,
           "' is called (", sm_.describe(m.exp->loc), ")");
    return check_name(e, hint);
  }

  // Expands a call of #expand macro `ms`.
  Expr* expand_macro(Call* c, Symbol* ms, Type* hint) {
    ProcLit* m = ms->proc;
    if (macros_.size() >= kMaxMacroDepth) {
      throw FinalError(c->loc, cat("#expand macro '", m->name, "' is expanded more than ", kMaxMacroDepth,
                                   " levels deep; does it expand itself without end?\n    the outermost expansion is '",
                                   macros_[0].exp->macro->name, "' at ", sm_.describe(macros_[0].exp->loc)));
    }
    c->proc_type = m->type;
    bind_args(c, m->type, m);  // in the caller's context
    auto* body = ast_.make<Block>(c->loc);
    auto* x = ast_.make<MacroExpansion>(c->loc, m, body);
    x->type = m->type->returns.empty() ? T.void_t : m->type->returns[0];
    ContextGuard guard(*this);
    MacroCtx ctx;
    ctx.id = ++next_macro_id_;
    ctx.exp = x;
    ctx.caller_scope = scope_;
    ctx.caller_in_run_operand = in_run_operand_;
    ctx.caller_defer_depth = defer_depth_;
    ctx.body_scope = new_scope(m->scope, proc_);
    body->scope = ctx.body_scope;
    if (pending_for_ && m->name == "for_expansion") {
      for_expansions_[pending_for_] = x;
      if (pending_for_->it_name != "it") ctx.rename_it = pending_for_->it_name;
      if (pending_for_->index_name != "it_index") ctx.rename_index = pending_for_->index_name;
      pending_for_ = nullptr;  // not for macros it expands in turn
    }
    // A bare break in the body may leave a loop of the caller, unless that
    // would cross a defer or the #run boundary.
    if (defer_depth_ > 0 || in_run_operand_) loops_.clear();
    macros_.push_back(ctx);
    scope_ = ctx.body_scope;
    in_run_operand_ = false;
    defer_depth_ = 0;
    std::string name = m->name.empty() ? "<anonymous>" : m->name;
    with_note("in the expansion of macro '" + name + "' at " + sm_.describe(c->loc), [&] {
      for (size_t i = 0; i < m->params.size(); ++i) {
        Decl* pd = m->params[i];
        Expr* a = c->resolved_args[i];
        const DeclName& pn = pd->names[0];
        // A for_expansion's For_Flags is a constant too (`#if flags & .REVERSE`).
        if (m->type->params[i] == for_flags_t_ && a->is_const()) {
          Symbol* s = declare(scope_, pn.name, SymKind::Const, pn.loc);
          s->type = for_flags_t_;
          s->cv = a->cv;
          s->state = Symbol::State::Resolved;
          continue;
        }
        if (m->type->params[i]->kind == TypeKind::Code) {  // a constant: #insert can use it
          if (!a->is_const())
            fail(a->loc, "the argument for Code parameter '", pn.name, "' of macro '", name,
                 "' must be a constant (#code ... or a Code constant)");
          Symbol* s = declare(scope_, pn.name, SymKind::Const, pn.loc);
          s->type = T.code_t;
          s->cv = a->cv;
          s->state = Symbol::State::Resolved;
          continue;
        }
        auto* d = ast_.make<Decl>(a->loc);  // other parameters are locals, evaluated once, in order
        d->names.push_back(pn);
        d->values.push_back(a);
        d->is_using = pd->is_using;
        body->stmts.push_back(d);
        check_local_decl(d);
      }
      auto* copy = static_cast<Block*>(clone_stmt(ast_, m->body));
      for (Stmt* st : copy->stmts) {
        body->stmts.push_back(st);
        check_stmt(st);
      }
      if (!m->type->returns.empty() && !always_returns(body, x))
        fail(m->loc, "macro '", name, "' does not return a value on all paths");
      return 0;
    });
    (void)hint;
    return x;
  }

  // ---- #modify

  struct ModifyResult {
    bool ok = true;
    std::string message;
    std::vector<Type*> types;  // the type variables afterwards, in binding order
  };
  std::map<std::pair<const Node*, std::string>, ModifyResult> modify_results_;

  template <typename F>
  static void for_each_return(Stmt* s, F& f) {
    if (!s) return;
    switch (s->kind) {
      case NK::Return: f(static_cast<Return*>(s)); return;
      case NK::Block:
        for (Stmt* x : static_cast<Block*>(s)->stmts) for_each_return(x, f);
        return;
      case NK::If:
        for_each_return(static_cast<If*>(s)->then_s, f);
        for_each_return(static_cast<If*>(s)->else_s, f);
        return;
      case NK::While: for_each_return(static_cast<While*>(s)->body, f); return;
      case NK::For: for_each_return(static_cast<For*>(s)->body, f); return;
      case NK::Defer: for_each_return(static_cast<Defer*>(s)->body, f); return;
      case NK::PushContext: for_each_return(static_cast<PushContext*>(s)->body, f); return;
      case NK::Switch:
        for (auto& c : static_cast<Switch*>(s)->cases)
          for (Stmt* x : c.body) for_each_return(x, f);
        return;
      case NK::StaticIf:
        for (Stmt* x : static_cast<StaticIf*>(s)->then_s) for_each_return(x, f);
        for (Stmt* x : static_cast<StaticIf*>(s)->else_s) for_each_return(x, f);
        return;
      default:
        return;
    }
  }

  // A checked-looking expression standing for type `t` (a constant).
  Expr* type_const(Type* t, SourceLoc loc) {
    auto* e = ast_.make<Ident>(loc, type_name(t));
    e->type = T.type_t;
    e->cv = ConstValue::of_type(t);
    return e;
  }

  // Runs the #modify block of polymorphic procedure `tmpl` for bindings `b`
  // (cached). The block becomes a procedure executed by the interpreter: the
  // type variables are its `Type` parameters, and each `return ok[, message];`
  // returns them along with the verdict. Compile-time values ($N) stay constants.
  ModifyResult eval_modify(Symbol* tmpl, const Bindings& b, SourceLoc loc) {
    ProcLit* p = tmpl->proc;
    SourceLoc ml = p->modify->loc;
    Type* rt = T.new_struct("#modify result");
    rt->fields.push_back(StructField{"ok", T.bool_t});
    rt->fields.push_back(StructField{"message", T.string_t});
    auto* mp = ast_.make<ProcLit>(ml);
    mp->name = "#modify of '" + p->name + "'";
    Bindings values;
    std::vector<Expr*> args;
    std::vector<std::string> vars;
    for (auto& x : b) {
      if (x.is_value) {
        values.push_back(x);
        continue;
      }
      rt->fields.push_back(StructField{x.name, T.type_t});
      auto* d = ast_.make<Decl>(x.loc);
      d->names.push_back({x.name, x.loc});
      d->type_expr = type_const(T.type_t, x.loc);
      mp->params.push_back(d);
      args.push_back(type_const(x.type, loc));
      vars.push_back(x.name);
    }
    T.layout_struct(rt);
    mp->returns.push_back(ReturnSpec{"", type_const(rt, ml), ml});
    mp->body = static_cast<Block*>(clone_stmt(ast_, p->modify));
    auto rewrite = [&](Return* r) {
      if (r->backtick || r->values.empty() || r->values.size() > 2)
        fail(r->loc, "#modify must return a bool, and optionally a message: return false, \"why\";");
      auto* lit = ast_.make<StructLit>(r->loc, type_const(rt, r->loc));
      lit->args.push_back(Arg{"", r->loc, r->values[0]});
      lit->args.push_back(Arg{"", r->loc, r->values.size() == 2 ? r->values[1] : ast_.make<StringLit>(r->loc, "")});
      for (auto& v : vars) lit->args.push_back(Arg{"", r->loc, ast_.make<Ident>(r->loc, v)});
      r->values = {lit};
    };
    for_each_return(mp->body, rewrite);
    Symbol* ms = new_symbol(mp->name, SymKind::Proc, ml);
    ms->proc = mp;
    ms->state = Symbol::State::Resolved;
    ComptimeMode mode(*this, true, mp, false);
    with_bindings(tmpl->scope, values, [&] {
      define_proc(mp, /*for_codegen=*/false);
      return 0;
    });
    ms->type = mp->type;
    auto* id = ast_.make<Ident>(loc, mp->name);
    id->sym = ms;
    auto* call = ast_.make<Call>(loc, id);
    for (Expr* a : args) call->args.push_back(Arg{"", loc, a});
    Expr* e = call;
    check(e, nullptr);
    ConstValue v = interp_.run(e, loc, true);
    ModifyResult r;
    const auto& es = *v.elems;
    r.ok = es.at(0).b;
    r.message = es.at(1).s;
    for (size_t i = 0; i < vars.size(); ++i) {
      const ConstValue& t = es.at(2 + i);
      if (t.kind != ConstValue::TypeV) fail(ml, "#modify of '", p->name, "' set '", vars[i], "' to null; it must be a type");
      r.types.push_back(t.t);
    }
    return r;
  }

  // Applies the #modify block of `tmpl` (if any) to the inferred bindings:
  // fails if it rejects them, and updates the type variables it changed.
  void run_modify(Symbol* tmpl, Bindings& b, SourceLoc loc) {
    ProcLit* p = tmpl->proc;
    if (!p->modify) return;
    auto key = std::make_pair((const Node*)p, binding_key(b));
    auto it = modify_results_.find(key);
    if (it == modify_results_.end()) {
      ModifyResult r = with_note("in #modify of '" + p->name + "' with " + bindings_desc(b), [&] {
        return eval_modify(tmpl, b, loc);
      });
      it = modify_results_.emplace(key, std::move(r)).first;
    }
    const ModifyResult& r = it->second;
    if (!r.ok)
      fail(loc, "#modify of '", p->name, "' rejected ", bindings_desc(b), r.message.empty() ? "" : ": ", r.message);
    size_t k = 0;
    for (auto& x : b)
      if (!x.is_value) x.type = r.types[k++];
  }

  // ------------------------------------------------------------------ calls

  Type* check_call(Call* c, Type* hint) {
    Symbol* qualified = nullptr;  // Module.proc(...)
    bool from_module = false;     // qualified by a module: only its exported overloads
    Symbol* qualified_set = nullptr;
    if (c->callee->kind == NK::Member && static_cast<Member*>(c->callee)->base) {
      auto* m = static_cast<Member*>(c->callee);
      if (Scope* ms = module_of(m->base)) {
        qualified = module_member(ms, m);
        from_module = true;
        qualified_set = ms->syms.at(m->name);  // the head of its overload set
        c->callee = ast_.make<Ident>(m->loc, m->name);
      } else if (Symbol* cs = member_constant(m)) {  // S.make(...): a procedure (or struct) constant of S
        qualified = cs;
        c->callee = ast_.make<Ident>(m->loc, m->name);
      }
    }
    // (A backtick callee is looked up in the macro caller's scope, below.)
    if (c->callee->kind == NK::Ident && !static_cast<Ident*>(c->callee)->sym &&
        !static_cast<Ident*>(c->callee)->backtick) {
      auto* id = static_cast<Ident*>(c->callee);
      Symbol* s = qualified ? qualified : find(id->name, id->loc).sym;
      if (s && is_poly_struct_decl(s)) {  // Vector(float, 3): an instance of a polymorphic struct
        resolve(s);
        c->cv = ConstValue::of_type(instantiate_struct(s, c->args, c->loc));
        return T.type_t;
      }
      if (s && s->kind == SymKind::Builtin) {
        id->sym = s;
        c->builtin = s->builtin;
        return check_builtin(c, hint);
      }
      if (s && is_proc_decl(s->decl, s->decl_index)) {
        std::vector<Symbol*> cands;
        Builtin fallback = Builtin::None;
        proc_distance_.clear();
        if (qualified) {
          Symbol* set = from_module ? qualified_set : s;
          cands = set->overloads.empty() ? std::vector<Symbol*>{set} : set->overloads;
          if (from_module) std::erase_if(cands, [](Symbol* o) { return !exported(o); });
        } else {
          gather_procs(id->name, cands, fallback);
        }
        bool any_poly = false;
        for (Symbol* o : cands) any_poly |= is_poly_proc_decl(o);
        if (cands.size() > 1 || fallback != Builtin::None || any_poly) {
          Symbol* pick = pick_overload(c, cands, id->name, fallback != Builtin::None);
          if (!pick) {
            c->builtin = fallback;
            return check_builtin(c, hint);
          }
          id->sym = pick;
          id->type = pick->type;
          // A polymorphic instance: its compile-time arguments are baked in.
          auto inst = poly_instances_.find(pick->proc);
          if (inst != poly_instances_.end()) c->args = runtime_args(c->args, inst->second.tmpl->proc, c->loc);
        } else if (qualified) {
          id->sym = s;
        }
      } else if (qualified) {
        id->sym = s;
      }
    }
    if (c->callee->kind == NK::Ident) {  // a call of an #expand macro is expanded in place
      auto* id = static_cast<Ident*>(c->callee);
      Symbol* s = id->sym;
      if (!s && !id->backtick) s = find(id->name, id->loc).sym;
      if (s && (s->kind == SymKind::Proc || is_proc_decl(s->decl, s->decl_index))) {
        resolve(s);
        if (s->kind == SymKind::Proc && s->proc && s->proc->is_macro) {
          id->sym = s;
          id->type = s->type;
          c->expansion = expand_macro(c, s, hint);
          return c->expansion->type;
        }
      }
    }
    Type* ct = check_value(c->callee, nullptr);
    if (c->callee->is_const() && c->callee->cv.kind == ConstValue::TypeV)
      fail(c->loc, "cannot call a type; use cast(", type_name(c->callee->cv.t), ") to convert");
    if (!ct->is_proc()) fail(c->loc, "cannot call a value of type ", type_name(ct));
    ProcLit* lit = nullptr;
    if (c->callee->kind == NK::Ident && static_cast<Ident*>(c->callee)->sym->kind == SymKind::Proc)
      lit = static_cast<Ident*>(c->callee)->sym->proc;
    c->proc_type = ct;
    if (!ct->c_call)
      require_context(c->loc, lit ? "calling " + proc_desc(lit) : std::string("calling a procedure value"));
    bind_args(c, ct, lit);
    if (ct->returns.empty()) return T.void_t;
    if (ct->returns.size() == 1) return ct->returns[0];
    return T.tuple(ct->returns);
  }

  // ---- overload resolution

  // Arguments whose type comes from the parameter they are passed to.
  static bool needs_hint(const Expr* e) {
    switch (e->kind) {
      case NK::ProcLit:  // a short lambda with untyped parameters
        for (const Decl* d : static_cast<const ProcLit*>(e)->params)
          if (!d->type_expr && d->values.empty()) return true;
        return false;
      case NK::StructLit: return static_cast<const StructLit*>(e)->type_expr == nullptr;
      case NK::ArrayLit: return static_cast<const ArrayLit*>(e)->elem_type_expr == nullptr;
      case NK::Member: return static_cast<const Member*>(e)->base == nullptr;
      case NK::AutoCast: return true;
      default: return false;
    }
  }

  // The cost of passing `e` as a parameter of type `p`, or -1 if it cannot be.
  int arg_cost(Expr* e, Type* p) {
    if (!e->type) {  // not checked yet: depends on the parameter type
      switch (e->kind) {
        case NK::StructLit: return p->kind == TypeKind::Struct ? 1 : -1;
        case NK::ArrayLit: return p->kind == TypeKind::Array ? 1 : -1;
        case NK::ProcLit:
          return p->is_proc() && p->params.size() == static_cast<ProcLit*>(e)->params.size() ? 1 : -1;
        case NK::Member:
          if (!p->is_enum()) return -1;
          for (auto& m : p->members)
            if (m.name == static_cast<Member*>(e)->name) return 1;
          return -1;
        default: return 3;  // xx
      }
    }
    Type* a = e->type;
    if (a == p) return 0;
    switch (a->kind) {
      case TypeKind::UntypedInt:
        if (p->kind == TypeKind::Int) return int_fits(e->cv.i, p) ? 1 : -1;
        if (p->kind == TypeKind::Float) return 2;
        if (p->is_enum() && p->is_flags) return 2;
        return p->kind == TypeKind::Any ? 4 : -1;
      case TypeKind::UntypedFloat:
        if (p->kind == TypeKind::Float) return 1;
        return p->kind == TypeKind::Any ? 4 : -1;
      case TypeKind::Null:
        return p->is_pointer() || p->is_proc() ? 1 : -1;
      default:
        if (a->isa && a->distinct_of == p) return 1;  // an isa type to its own base: the nearest conversion
        if (implicit_ok(a, p)) return p->kind == TypeKind::Any ? 4 : 2;
        return -1;
    }
  }

  // Total cost of calling procedure `lit` (type pt) with c's arguments, or -1.
  int call_cost(Call* c, Type* pt, ProcLit* lit) {
    size_t n = pt->params.size();
    int va = pt->vararg;
    size_t fixed = va >= 0 ? (size_t)va : n;
    std::vector<Expr*> out(n, nullptr);
    int cost = 0;
    bool spread = false;
    size_t pos = 0;
    for (auto& a : c->args) {
      if (!a.name.empty()) {
        size_t idx = n;
        for (size_t i = 0; i < n; ++i)
          if (lit->params[i]->names[0].name == a.name) idx = i;
        if (idx == n || out[idx] || (int)idx == va) return -1;
        out[idx] = a.value;
      } else if (a.spread) {
        if (va < 0 || pos < fixed || spread) return -1;
        spread = true;
        int k = arg_cost(a.value, pt->params[va]);
        if (k < 0) return -1;
        cost += k + 1;
      } else if (pos < fixed) {
        out[pos++] = a.value;
      } else if (va >= 0) {
        if (spread) return -1;
        int k = arg_cost(a.value, pt->params[va]->elem);
        if (k < 0) return -1;
        cost += k + 1;  // prefer a fixed-arity overload
      } else {
        return -1;
      }
    }
    for (size_t i = 0; i < n; ++i) {
      if ((int)i == va) continue;
      if (!out[i]) {
        if (lit->params[i]->values.empty()) return -1;
        continue;
      }
      int k = arg_cost(out[i], pt->params[i]);
      if (k < 0) return -1;
      cost += k;
    }
    return cost;
  }

  // Chooses the overload of `name` that fits c's arguments best. Returns null
  // when none fits and `builtin_fallback` is set. A polymorphic candidate is
  // instantiated for the arguments; the instance is returned when it wins.
  Symbol* pick_overload(Call* c, const std::vector<Symbol*>& cands, const std::string& name, bool builtin_fallback) {
    for (auto& a : c->args)
      if (!needs_hint(a.value)) check_rv(a.value, nullptr);
    Symbol* best = nullptr;
    int best_cost = -1;
    std::vector<Symbol*> tied;
    std::unordered_map<Symbol*, Symbol*> instance;  // polymorphic candidate -> its instance for this call
    std::unordered_map<Symbol*, std::string> why;   // polymorphic candidate -> why it does not fit
    for (Symbol* s : cands) {
      resolve(s);
      int k = -1;
      if (s->kind == SymKind::PolyProc) {
        if (cands.size() == 1 && !builtin_fallback) {
          instance[s] = poly_for_call(c, s, k);  // the only candidate: report exactly why it does not fit
        } else {
          Context ctx = save();
          try {
            instance[s] = poly_for_call(c, s, k);
          } catch (const CompileError& e) {
            restore(ctx);
            why[s] = e.what();
            continue;
          }
        }
      } else if (s->kind == SymKind::Proc) {
        k = call_cost(c, s->type, s->proc);
      } else {
        continue;
      }
      if (k < 0) continue;
      if (!best || k < best_cost) {
        best = s;
        best_cost = k;
        tied = {s};
      } else if (k == best_cost) {
        tied.push_back(s);
      }
    }
    if (tied.size() > 1) {  // an equally good non-polymorphic procedure wins over a polymorphic one
      std::vector<Symbol*> plain;
      for (Symbol* s : tied)
        if (s->kind == SymKind::Proc) plain.push_back(s);
      if (!plain.empty()) {
        tied = plain;
        best = plain[0];
      }
    }
    if (tied.size() > 1 && !proc_distance_.empty()) {
      int nearest = INT32_MAX;
      for (Symbol* s : tied) nearest = std::min(nearest, proc_distance_.count(s) ? proc_distance_[s] : INT32_MAX);
      std::vector<Symbol*> near;
      for (Symbol* s : tied)
        if (proc_distance_.count(s) && proc_distance_[s] == nearest) near.push_back(s);
      if (!near.empty()) {
        tied = near;
        best = near[0];
      }
    }
    if (tied.size() > 1) {
      std::string where;
      for (Symbol* s : tied) where += "\n    " + sm_.describe(s->loc) + ": " + candidate_str(s, instance);
      fail(c->loc, "ambiguous call to overloaded procedure '", name, "'; these fit equally well:", where);
    }
    if (!best && !builtin_fallback) {
      std::string args, where;
      for (auto& a : c->args) {
        if (!args.empty()) args += ", ";
        args += a.value->type ? type_name(a.value->type) : std::string("?");
      }
      for (Symbol* s : cands) {
        where += "\n    " + sm_.describe(s->loc) + ": " + candidate_str(s, instance);
        if (why.count(s)) where += " (" + first_line(why[s]) + ")";
      }
      fail(c->loc, "no overload of '", name, "' accepts (", args, "); candidates:", where);
    }
    if (best && best->kind == SymKind::PolyProc) {
      best = instance.at(best);
      use_instance(best, c->loc);
    }
    return best;
  }

  static std::string first_line(const std::string& s) { return s.substr(0, s.find('\n')); }

  std::string candidate_str(Symbol* s, const std::unordered_map<Symbol*, Symbol*>& instance) {
    if (s->kind != SymKind::PolyProc) return type_name(s->type);
    std::string str = "polymorphic " + proc_pattern_str(s->proc);
    auto it = instance.find(s);
    if (it != instance.end()) str += " as " + type_name(it->second->type);
    return str;
  }

  // ---- operator overloading

  // Types whose operators may be overloaded (distinct types too: `operator +` on Handle).
  static bool overloadable(Type* t) {
    return t->kind == TypeKind::Struct || t->kind == TypeKind::Array || t->distinct_of;
  }

  // A call of `operator<op>` with `args` (checked, except hint-dependent ones),
  // or null if no `operator<op>` is declared or none fits. A first parameter
  // `*T` accepts an lvalue of type T (its address is passed).
  Expr* operator_call(const std::string& op, std::vector<Expr*> args, SourceLoc loc) {
    std::string name = "operator" + op;
    // The operators of every enclosing scope and imported module (e.g. the
    // program's own and Math's vector operators).
    std::vector<Symbol*> cands;
    Builtin no_builtin = Builtin::None;
    gather_procs(name, cands, no_builtin);
    if (cands.empty()) return nullptr;
    for (Expr*& a : args)
      if (!needs_hint(a)) check_rv(a, nullptr);
    struct Choice { Symbol* sym; bool swap; bool addr; int cost; bool poly; };
    std::vector<Choice> best;
    auto consider = [&](const Choice& ch) {
      if (best.empty() || ch.cost < best[0].cost) best = {ch};
      else if (ch.cost == best[0].cost) best.push_back(ch);
    };
    for (Symbol* s : cands) {
      resolve(s);
      if (s->kind == SymKind::PolyProc) {
        ProcLit* p = s->proc;
        bool baked = false;
        for (Decl* d : p->params) baked |= d->is_baked;
        if (p->variadic || baked || p->params.size() != args.size()) continue;
        for (int swap = 0; swap < (p->symmetric && args.size() == 2 ? 2 : 1); ++swap) {
          ArgMap m;
          m.params = args;
          if (swap) std::swap(m.params[0], m.params[1]);
          // A first parameter `*Pattern` accepts an lvalue (its address is passed).
          bool addr = false;
          Expr* first = m.params[0];
          if (p->params[0]->type_expr && p->params[0]->type_expr->kind == NK::PtrType && first->type &&
              !first->type->is_pointer() && first->type->kind != TypeKind::Null && is_lvalue(first)) {
            Expr* u = ast_.make<Unary>(first->loc, UnOp::AddrOf, first);
            check(u, nullptr);
            m.params[0] = u;
            addr = true;
          }
          Context ctx = save();
          try {
            Symbol* inst = instantiate_proc(s, infer_poly(s, m, loc), loc);
            int cost = 0;
            for (size_t i = 0; i < m.params.size() && cost >= 0; ++i) {
              int k = arg_cost(m.params[i], inst->type->params[i]);
              cost = k < 0 ? -1 : cost + k;
            }
            if (cost >= 0) consider({inst, swap != 0, addr, cost, true});
          } catch (const CompileError&) {
            restore(ctx);
          }
        }
        continue;
      }
      if (s->kind != SymKind::Proc || s->type->variadic || s->type->params.size() != args.size()) continue;
      for (int swap = 0; swap < (s->proc->symmetric && args.size() == 2 ? 2 : 1); ++swap) {
        std::vector<Expr*> order = args;
        if (swap) std::swap(order[0], order[1]);
        bool addr = false;
        int cost = 0;
        for (size_t i = 0; i < order.size() && cost >= 0; ++i) {
          Type* p = s->type->params[i];
          int k = arg_cost(order[i], p);
          if (k < 0 && i == 0 && p->is_pointer() && order[0]->type && p->elem == order[0]->type &&
              is_lvalue(order[0])) {
            k = 0;
            addr = true;
          }
          cost = k < 0 ? -1 : cost + k;
        }
        if (cost < 0) continue;
        consider({s, swap != 0, addr, cost, false});
      }
    }
    if (best.empty()) return nullptr;
    if (best.size() > 1) {  // an equally good non-polymorphic operator wins over a polymorphic one
      std::vector<Choice> plain;
      for (auto& ch : best)
        if (!ch.poly) plain.push_back(ch);
      if (!plain.empty()) best = plain;
    }
    if (best.size() > 1) fail(loc, "ambiguous use of overloaded '", name, "'");
    Choice ch = best[0];
    if (ch.poly) use_instance(ch.sym, loc);
    if (ch.swap) std::swap(args[0], args[1]);
    if (ch.addr) {
      auto* u = ast_.make<Unary>(args[0]->loc, UnOp::AddrOf, args[0]);
      args[0] = u;
    }
    auto* id = ast_.make<Ident>(loc, name);
    id->sym = ch.sym;
    auto* c = ast_.make<Call>(loc, id);
    for (Expr* a : args) c->args.push_back(Arg{"", loc, a});
    Expr* e = c;
    check(e, nullptr);
    return e;
  }

  // `..array` as argument `a`: only for the variadic parameter, and as its only argument.
  void check_spread(const Arg& a, size_t pos, size_t fixed, bool variadic, size_t n_extra) {
    if (!variadic) fail(a.value->loc, "'..' passes an array to a variadic parameter, but this procedure has none");
    if (pos < fixed) fail(a.value->loc, "'..' passes an array to the variadic parameter; arguments for the ",
                          fixed - pos, " parameter", fixed - pos == 1 ? "" : "s", " before it are missing");
    if (n_extra > 0) fail_after_spread(a.value);
  }
  // Another argument for the variadic parameter next to a `..array`.
  [[noreturn]] void fail_after_spread(Expr* e) {
    fail(e->loc, "an array passed with '..' must be the only argument of the variadic parameter");
  }

  void check_spread_value(Expr*& e, Type* view) {
    Type* t = check_value(e, nullptr);
    if (t->kind != TypeKind::Array)
      fail(e->loc, "'..' needs an array, not ", type_name(t));
    if (t->elem != view->elem)
      fail(e->loc, "'..' passes ", type_name(t), " to a variadic parameter of type ..", type_name(view->elem));
    coerce(e, view);
  }

  // Built-ins take no `..array` (print and sprint check theirs themselves).
  static void reject_spread(Call* c, const char* name) {
    for (auto& a : c->args)
      if (a.spread) fail(a.value->loc, "'", name, "' has no variadic parameter to pass '..' to");
  }

  void bind_args(Call* c, Type* pt, ProcLit* lit) {
    size_t n = pt->params.size();
    int va = pt->vararg;
    size_t fixed = va >= 0 ? (size_t)va : n;
    std::vector<Expr*> out(n, nullptr);
    std::vector<Expr*> var_args;
    Expr* spread = nullptr;
    size_t pos = 0;
    bool named_seen = false;
    for (auto& a : c->args) {
      if (!a.name.empty()) {
        named_seen = true;
        if (!lit) fail(a.name_loc, "named arguments need a direct call to a named procedure");
        size_t idx = n;
        for (size_t i = 0; i < n; ++i)
          if (lit->params[i]->names[0].name == a.name) idx = i;
        if (idx == n) fail(a.name_loc, "procedure '", lit->name, "' has no parameter named '", a.name, "'");
        if (out[idx]) fail(a.name_loc, "parameter '", a.name, "' is given twice");
        if ((int)idx == va) fail(a.name_loc, "cannot pass a variadic parameter by name");
        out[idx] = a.value;
        continue;
      }
      if (named_seen) fail(a.value->loc, "positional arguments must come before named ones");
      if (a.spread) {
        check_spread(a, pos, fixed, va >= 0, var_args.size());
        spread = a.value;
        continue;
      }
      if (pos < fixed) {
        out[pos++] = a.value;
      } else if (va >= 0) {
        if (spread) fail_after_spread(a.value);
        var_args.push_back(a.value);
      } else {
        fail(a.value->loc, "too many arguments: expected ", n);
      }
    }
    for (size_t i = 0; i < n; ++i) {
      if ((int)i == va) continue;
      if (out[i]) {
        check_value(out[i], pt->params[i]);
        continue;
      }
      if (lit && !lit->params[i]->values.empty()) {
        Expr* dv = lit->params[i]->values[0];  // default value (already checked)
        out[i] = is_caller_location(dv) ? caller_location_at(c->loc) : dv;
        continue;
      }
      std::string pname = lit ? "'" + lit->params[i]->names[0].name + "'" : std::to_string(i + 1);
      if (!lit && va >= 0 && (int)i > va)
        fail(c->loc, "missing argument for parameter ", pname, ": it follows the variadic parameter, so it can only "
             "be passed by name, which a call through a procedure value cannot do");
      fail(c->loc, "missing argument for parameter ", pname);
    }
    if (spread) {
      check_spread_value(spread, pt->params[va]);
      out[va] = spread;
    } else if (va >= 0) {
      Type* elem = pt->params[va]->elem;
      auto* pack = ast_.make<ArrayLit>(c->loc, nullptr);
      for (Expr* v : var_args) {
        check_value(v, elem);
        pack->elems.push_back(v);
      }
      pack->type = T.array_of(elem, ArrayKind::Fixed, (int64_t)var_args.size());
      complete_type(pack->type, c->loc);
      out[va] = ast_.make<ImplicitConv>(c->loc, pack, pt->params[va]);
    }
    c->resolved_args = std::move(out);
  }

  // Positional-only argument helper for built-ins.
  std::vector<Expr*>& positional(Call* c, size_t min, size_t max, const char* name) {
    reject_spread(c, name);
    for (auto& a : c->args)
      if (!a.name.empty()) fail(a.name_loc, "'", name, "' does not take named arguments");
    if (c->args.size() < min || c->args.size() > max) {
      if (min == max) fail(c->loc, "'", name, "' takes ", min, " argument", min == 1 ? "" : "s");
      fail(c->loc, "'", name, "' takes ", min, " to ", max, " arguments");
    }
    c->resolved_args.clear();
    for (auto& a : c->args) c->resolved_args.push_back(a.value);
    return c->resolved_args;
  }

  // Arguments of a built-in with parameters `names`: positional ones first,
  // then by name. The first `required` must be given; others may be null.
  std::vector<Expr*> builtin_args(Call* c, std::initializer_list<const char*> names, size_t required,
                                  const char* what) {
    reject_spread(c, what);
    std::vector<const char*> ns(names);
    std::vector<Expr*> out(ns.size(), nullptr);
    size_t pos = 0;
    bool named_seen = false;
    for (auto& a : c->args) {
      if (a.name.empty()) {
        if (named_seen) fail(a.value->loc, "positional arguments must come before named ones");
        if (pos >= ns.size()) fail(a.value->loc, "too many arguments for '", what, "'");
        out[pos++] = a.value;
        continue;
      }
      named_seen = true;
      size_t i = 0;
      while (i < ns.size() && a.name != ns[i]) ++i;
      if (i == ns.size()) fail(a.name_loc, "'", what, "' has no parameter named '", a.name, "'");
      if (out[i]) fail(a.name_loc, "parameter '", a.name, "' is given twice");
      out[i] = a.value;
    }
    for (size_t i = 0; i < required; ++i)
      if (!out[i]) fail(c->loc, "'", what, "' is missing its '", ns[i], "' argument");
    return out;
  }

  // `initialized = b` of New / NewArray (a constant).
  void set_initialized_arg(Call* c, Expr* e) {
    if (!e) return;
    check_value(e, T.bool_t);
    if (!e->is_const()) fail(e->loc, "'initialized' must be a compile-time constant");
    c->no_init = !e->cv.b;
  }

  int count_format_specs(const std::string& fmt) {
    int n = 0;
    for (size_t i = 0; i < fmt.size(); ++i) {
      if (fmt[i] != '%') continue;
      if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
        ++i;
        continue;
      }
      ++n;
    }
    return n;
  }

  Type* check_numeric_args(Call* c, std::vector<Expr*>& args, bool float_only, const char* name) {
    std::vector<Type*> ts;
    for (Expr*& a : args) ts.push_back(check_rv(a, nullptr));
    Type* t = nullptr;
    for (Type* x : ts)
      if (!x->is_untyped()) {
        if (!t || implicit_ok(t, x)) t = x;
      }
    if (!t) {
      bool any_float = false;
      for (Type* x : ts) any_float |= x->is_float();
      t = (any_float || float_only) ? T.f32 : T.s64;
    }
    if (float_only && t->kind != TypeKind::Float) fail(c->loc, "'", name, "' needs float arguments, not ", type_name(t));
    if (!t->is_numeric()) fail(c->loc, "'", name, "' needs numeric arguments, not ", type_name(t));
    for (Expr*& a : args) coerce(a, t);
    return t;
  }

  static std::string callee_name(Call* c) {
    return c->callee->kind == NK::Ident ? static_cast<Ident*>(c->callee)->name : std::string("sprint");
  }

  Type* check_builtin(Call* c, Type* hint) {
    switch (c->builtin) {
      case Builtin::SizeOf: {
        auto& a = positional(c, 1, 1, "size_of");
        Type* t = resolve_type(a[0]);
        complete_type(t, c->loc);
        c->type_arg = t;
        c->cv = ConstValue::of_int(t->size);
        return T.untyped_int;
      }
      case Builtin::TypeInfo: {  // type_info(T): T's Type_Info, as the Type_Info_* struct for its kind
        auto& a = positional(c, 1, 1, "type_info");
        Type* t = resolve_type(a[0]);
        if (t->is_untyped()) fail(a[0]->loc, "type_info needs a type, not an untyped constant");
        complete_type(t, c->loc);
        c->type_arg = t;
        return T.pointer_to(type_info_struct_for(t));
      }
      case Builtin::TypeOf: {
        auto& a = positional(c, 1, 1, "type_of");
        Type* t = check_value(a[0], nullptr);
        c->type_arg = t;
        c->cv = ConstValue::of_type(t);
        return T.type_t;
      }
      case Builtin::Print:
      case Builtin::Sprint: {
        const char* name = c->builtin == Builtin::Print ? "print" : "sprint";
        Expr* to_stderr = nullptr;
        std::vector<Expr*> args;
        Expr* spread = nullptr;
        for (auto& a : c->args) {
          if (a.spread) {
            if (args.size() != 1 || spread)
              fail(a.value->loc, "'", name, "' takes '..' only right after the format string, as its only argument");
            spread = a.value;
            continue;
          }
          if (spread && a.name.empty())
            fail(a.value->loc, "an array passed with '..' must be the only argument after the format string");
          if (!a.name.empty()) {
            if (a.name == "to_standard_error" && c->builtin == Builtin::Print) {
              to_stderr = a.value;
              continue;
            }
            fail(a.name_loc, "'", name, "' has no parameter named '", a.name, "'");
          }
          args.push_back(a.value);
        }
        if (args.empty()) fail(c->loc, "'", name, "' needs a format string");
        check_value(args[0], T.string_t);
        if (spread) {
          check_spread_value(spread, T.array_of(T.any_t, ArrayKind::View));
          c->spread_args = true;
          args.push_back(spread);
        } else if (args[0]->is_const()) {
          int specs = count_format_specs(args[0]->cv.s);
          if (specs != (int)args.size() - 1)
            fail(c->loc, "format string has ", specs, " '%' placeholder", specs == 1 ? "" : "s", " but ",
                 args.size() - 1, " argument", args.size() == 2 ? " was" : "s were", " given");
        }
        for (size_t i = spread ? args.size() : 1; i < args.size(); ++i) {
          check_rv(args[i], nullptr);
          if (args[i]->type->kind == TypeKind::Proc && args[i]->kind == NK::Ident) {
            // fine: printed as an address
          }
          complete_type(args[i]->type, args[i]->loc);
          coerce(args[i], T.any_t);
        }
        if (to_stderr) check_value(to_stderr, T.bool_t);
        c->resolved_args = args;
        c->resolved_args.push_back(to_stderr);  // may be null
        if (c->builtin == Builtin::Print) return T.s64;
        require_context(c->loc, cat("'", callee_name(c), "'"));
        if (callee_name(c) == "tprint") {  // sprint with temporary_allocator
          auto* ta = ast_.make<Ident>(c->loc, "temporary_allocator");
          ta->sym = preload_sym("temporary_allocator");
          Expr* e = ta;
          set_allocator_arg(c, e);
        }
        return T.string_t;
      }
      case Builtin::WriteString: {
        Expr* to_stderr = nullptr;
        std::vector<Expr*> args;
        for (auto& a : c->args) {
          if (!a.name.empty()) {
            if (a.name != "to_standard_error") fail(a.name_loc, "unknown parameter '", a.name, "'");
            to_stderr = a.value;
          } else {
            args.push_back(a.value);
          }
        }
        if (args.empty() || args.size() > 2) fail(c->loc, "'write_string' takes a string");
        if (args.size() == 2) to_stderr = args[1];
        check_value(args[0], T.string_t);
        if (to_stderr) check_value(to_stderr, T.bool_t);
        c->resolved_args = {args[0], to_stderr};
        return T.void_t;
      }
      case Builtin::Alloc: {
        auto args = builtin_args(c, {"size", "allocator"}, 1, "alloc");
        require_context(c->loc, "'alloc'");
        c->resolved_args = {args[0]};
        check_value(c->resolved_args[0], T.s64);
        set_allocator_arg(c, args[1]);
        return T.void_ptr;
      }
      case Builtin::Free: {
        auto args = builtin_args(c, {"memory", "allocator"}, 1, "free");
        require_context(c->loc, "'free'");
        c->resolved_args = {args[0]};
        set_allocator_arg(c, args[1]);
        auto& a = c->resolved_args;
        Type* t = check_value(a[0], nullptr);
        if (t->is_pointer()) coerce(a[0], T.void_ptr);
        else if (t->is_view() || t->kind == TypeKind::String) {
          // free(array_view) / free(string) frees its data
        } else {
          fail(a[0]->loc, "'free' needs a pointer, string or array view, not ", type_name(t));
        }
        return T.void_t;
      }
      case Builtin::Realloc: {
        auto args = builtin_args(c, {"memory", "size", "old_size", "allocator"}, 3, "realloc");
        require_context(c->loc, "'realloc'");
        c->resolved_args = {args[0], args[1], args[2]};
        auto& a = c->resolved_args;
        check_value(a[0], T.void_ptr);
        check_value(a[1], T.s64);
        check_value(a[2], T.s64);
        set_allocator_arg(c, args[3]);
        return T.void_ptr;
      }
      case Builtin::New: {
        auto args = builtin_args(c, {"T", "initialized", "allocator"}, 1, "New");
        require_context(c->loc, "'New'");
        c->resolved_args = {args[0]};
        Type* t = resolve_type(c->resolved_args[0]);
        complete_type(t, c->loc);
        c->type_arg = t;
        set_initialized_arg(c, args[1]);
        set_allocator_arg(c, args[2]);
        want_aligned_allocation(t);
        return T.pointer_to(t);
      }
      case Builtin::NewArray: {
        auto args = builtin_args(c, {"count", "T", "initialized", "allocator"}, 2, "NewArray");
        require_context(c->loc, "'NewArray'");
        c->resolved_args = {args[0], args[1]};
        auto& a = c->resolved_args;
        check_value(a[0], T.s64);
        Type* t = resolve_type(a[1]);
        complete_type(t, c->loc);
        c->type_arg = t;
        set_initialized_arg(c, args[2]);
        set_allocator_arg(c, args[3]);
        want_aligned_allocation(t);
        return T.array_of(t, ArrayKind::View);
      }
      case Builtin::RawAllocAligned: {
        auto& a = positional(c, 2, 2, "__ojay_raw_alloc_aligned");
        check_value(a[0], T.s64);
        check_value(a[1], T.s64);
        return T.void_ptr;
      }
      case Builtin::RawAlloc: {
        auto& a = positional(c, 1, 1, "__ojay_raw_alloc");
        check_value(a[0], T.s64);
        return T.void_ptr;
      }
      case Builtin::RawFree: {
        auto& a = positional(c, 1, 1, "__ojay_raw_free");
        check_value(a[0], T.void_ptr);
        return T.void_t;
      }
      case Builtin::RawRealloc: {
        auto& a = positional(c, 3, 3, "__ojay_raw_realloc");
        check_value(a[0], T.void_ptr);
        check_value(a[1], T.s64);
        check_value(a[2], T.s64);
        return T.void_ptr;
      }
      case Builtin::ArrayAdd: {
        require_context(c->loc, "'array_add'");
        auto& a = positional(c, 1, 2, "array_add");
        Type* t = check_value(a[0], nullptr);
        if (!t->is_pointer() || !t->elem->is_dynamic())
          fail(a[0]->loc, "'array_add' needs a pointer to a dynamic array ([..]T), e.g. array_add(*arr, x)");
        Type* elem = t->elem->elem;
        c->type_arg = elem;
        complete_type(elem, c->loc);
        want_aligned_allocation(elem);
        if (a.size() == 1) return T.pointer_to(elem);
        check_value(a[1], elem);
        return T.void_t;
      }
      case Builtin::ArrayReserve: {
        require_context(c->loc, "'array_reserve'");
        auto& a = positional(c, 2, 2, "array_reserve");
        Type* t = check_value(a[0], nullptr);
        if (!t->is_pointer() || !t->elem->is_dynamic())
          fail(a[0]->loc, "'array_reserve' needs a pointer to a dynamic array ([..]T), e.g. array_reserve(*arr, 100)");
        c->type_arg = t->elem->elem;
        complete_type(c->type_arg, c->loc);
        want_aligned_allocation(c->type_arg);
        check_value(a[1], T.s64);
        return T.void_t;
      }
      case Builtin::ArrayFree: {
        require_context(c->loc, "'array_free'");
        auto& a = positional(c, 1, 1, "array_free");
        Type* t = check_value(a[0], nullptr);
        if (t->kind != TypeKind::Array || t->is_fixed_array())
          fail(a[0]->loc, "'array_free' needs a dynamic array or array view");
        return T.void_t;
      }
      case Builtin::ArrayReset: {
        require_context(c->loc, "'array_reset'");
        auto& a = positional(c, 1, 1, "array_reset");
        Type* t = check_value(a[0], nullptr);
        if (!t->is_pointer() || !t->elem->is_dynamic()) fail(a[0]->loc, "'array_reset' needs a pointer to a dynamic array");
        return T.void_t;
      }
      case Builtin::Assert: {
        // Jai's assert :: (arg: bool, message := "", args: ..Any, loc := #caller_location):
        // `loc` follows the (not yet supported) format arguments, so it is passed by name.
        auto args = builtin_args(c, {"arg", "message", "loc"}, 1, "assert");
        if (c->args.size() >= 3 && c->args[2].name.empty())
          fail(c->args[2].value->loc, "'assert' does not take format arguments yet (pass 'loc' by name)");
        // resolved: condition, message (or null), and the file and line to report (or null for the call site)
        c->resolved_args = {args[0], args[1], nullptr, nullptr};
        auto& a = c->resolved_args;
        check_cond(a[0]);
        if (a[1]) check_value(a[1], T.string_t);
        if (args[2]) {
          check_value(args[2], source_loc_t_);
          // Read only when the assertion fails; `loc` is evaluated once for each member.
          a[2] = ast_.make<Member>(args[2]->loc, args[2], "fully_pathed_filename");
          a[3] = ast_.make<Member>(args[2]->loc, args[2], "line_number");
          check_value(a[2], T.string_t);
          check_value(a[3], T.s64);
        }
        return T.void_t;
      }
      case Builtin::CommandLineArguments:
        positional(c, 0, 0, "get_command_line_arguments");
        return T.array_of(T.string_t, ArrayKind::View);
      case Builtin::Exit: {
        auto& a = positional(c, 1, 1, "exit");
        check_value(a[0], T.s32);
        return T.void_t;
      }
      case Builtin::Memcpy: {
        auto& a = positional(c, 3, 3, "memcpy");
        check_value(a[0], T.void_ptr);
        check_value(a[1], T.void_ptr);
        check_value(a[2], T.s64);
        return T.void_t;
      }
      case Builtin::Memset: {
        auto& a = positional(c, 3, 3, "memset");
        check_value(a[0], T.void_ptr);
        check_value(a[1], T.u8);
        check_value(a[2], T.s64);
        return T.void_t;
      }
      case Builtin::Min:
      case Builtin::Max: {
        auto& a = positional(c, 2, 2, c->builtin == Builtin::Min ? "min" : "max");
        Type* t = check_numeric_args(c, a, false, c->builtin == Builtin::Min ? "min" : "max");
        return t;
      }
      case Builtin::Abs: {
        auto& a = positional(c, 1, 1, "abs");
        return check_numeric_args(c, a, false, "abs");
      }
      case Builtin::ToCString: {
        require_context(c->loc, "'to_c_string'");
        auto& a = positional(c, 1, 1, "to_c_string");
        check_value(a[0], T.string_t);
        return T.pointer_to(T.u8);
      }
      case Builtin::Sqrt: case Builtin::Sin: case Builtin::Cos: case Builtin::Tan: case Builtin::Floor:
      case Builtin::Ceil: case Builtin::Round: case Builtin::Exp: case Builtin::Log: {
        auto& a = positional(c, 1, 1, "math function");
        (void)hint;
        return check_numeric_args(c, a, true, "math function");
      }
      case Builtin::VolatileLoad: {
        auto& a = positional(c, 1, 1, "volatile_load");
        Type* t = check_value(a[0], nullptr);
        if (!t->is_pointer() || t->elem->kind == TypeKind::Void)
          fail(a[0]->loc, "'volatile_load' needs a typed pointer, not ", type_name(t));
        if (!is_c_scalar(t->elem))
          fail(a[0]->loc, "'volatile_load' works on integers, floats, enums, bools and pointers, not ",
               type_name(t->elem));
        return t->elem;
      }
      case Builtin::VolatileStore: {
        auto& a = positional(c, 2, 2, "volatile_store");
        Type* t = check_value(a[0], nullptr);
        if (!t->is_pointer() || t->elem->kind == TypeKind::Void)
          fail(a[0]->loc, "'volatile_store' needs a typed pointer, not ", type_name(t));
        if (!is_c_scalar(t->elem))
          fail(a[0]->loc, "'volatile_store' works on integers, floats, enums, bools and pointers, not ",
               type_name(t->elem));
        check_value(a[1], t->elem);
        return T.void_t;
      }
      case Builtin::Atan2:
      case Builtin::Pow: {
        auto& a = positional(c, 2, 2, c->builtin == Builtin::Pow ? "pow" : "atan2");
        return check_numeric_args(c, a, true, c->builtin == Builtin::Pow ? "pow" : "atan2");
      }
      default:
        fail(c->loc, "unsupported built-in");
    }
  }
};

}  // namespace

void analyze(AstContext& ast, SourceManager& sm, TypeTable& types, Program& prog, const SemaOptions& opts) {
  Checker c(ast, sm, types, prog, opts);
  c.run();
}

}  // namespace ojay
