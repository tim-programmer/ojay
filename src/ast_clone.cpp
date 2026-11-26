// Deep copies of syntax trees. Polymorphic procedures and structs are
// instantiated by cloning the template's ProcLit / StructDef and checking the
// copy with the type variables bound; the template itself is never checked.
//
// Only fields set by the parser (and the driver's module ids) are copied.
// Sema annotations - types, constant values, symbols, scopes, overload
// rewrites - stay unset so the copy is checked from scratch.
#include "ast.h"

namespace ojay {
namespace {

class Cloner {
 public:
  explicit Cloner(AstContext& ast) : ast_(ast) {}

  Expr* expr(const Expr* e) {
    if (!e) return nullptr;
    switch (e->kind) {
      case NK::IntLit: return ast_.make<IntLit>(e->loc, as<IntLit>(e)->value);
      case NK::FloatLit: return ast_.make<FloatLit>(e->loc, as<FloatLit>(e)->value);
      case NK::StringLit: {
        auto* s = as<StringLit>(e);
        auto* n = ast_.make<StringLit>(e->loc, s->value);
        n->here_string = s->here_string;
        return n;
      }
      case NK::BoolLit: return ast_.make<BoolLit>(e->loc, as<BoolLit>(e)->value);
      case NK::NullLit: return ast_.make<NullLit>(e->loc);
      case NK::Ident: {
        auto* id = as<Ident>(e);
        auto* n = ast_.make<Ident>(e->loc, id->name);
        n->poly = id->poly;
        n->backtick = id->backtick;
        n->constraint = expr(id->constraint);
        n->constraint_interface = id->constraint_interface;
        return n;
      }
      case NK::Unary: {
        auto* u = as<Unary>(e);
        return ast_.make<Unary>(e->loc, u->op, expr(u->operand));
      }
      case NK::Binary: {
        auto* b = as<Binary>(e);
        return ast_.make<Binary>(e->loc, b->op, expr(b->lhs), expr(b->rhs));
      }
      case NK::Call: {
        auto* c = as<Call>(e);
        auto* n = ast_.make<Call>(e->loc, expr(c->callee));
        n->args = args(c->args);
        n->force_inline = c->force_inline;
        return n;
      }
      case NK::Index: {
        auto* i = as<Index>(e);
        return ast_.make<Index>(e->loc, expr(i->base), expr(i->index));
      }
      case NK::Member: {
        auto* m = as<Member>(e);
        return ast_.make<Member>(e->loc, expr(m->base), m->name);
      }
      case NK::Cast: {
        auto* c = as<Cast>(e);
        auto* n = ast_.make<Cast>(e->loc, expr(c->type_expr), expr(c->operand));
        n->truncate = c->truncate;
        n->no_check = c->no_check;
        return n;
      }
      case NK::AutoCast: return ast_.make<AutoCast>(e->loc, expr(as<AutoCast>(e)->operand));
      case NK::StructLit: {
        auto* s = as<StructLit>(e);
        auto* n = ast_.make<StructLit>(e->loc, expr(s->type_expr));
        n->args = args(s->args);
        return n;
      }
      case NK::ArrayLit: {
        auto* a = as<ArrayLit>(e);
        auto* n = ast_.make<ArrayLit>(e->loc, expr(a->elem_type_expr));
        for (Expr* x : a->elems) n->elems.push_back(expr(x));
        return n;
      }
      case NK::ProcLit: return proc(as<ProcLit>(e));
      case NK::StructDef: {
        auto* s = as<StructDef>(e);
        auto* n = ast_.make<StructDef>(e->loc);
        n->is_union = s->is_union;
        for (Decl* d : s->params) n->params.push_back(decl(d));
        for (Decl* d : s->members) n->members.push_back(decl(d));
        return n;
      }
      case NK::EnumDef: {
        auto* en = as<EnumDef>(e);
        auto* n = ast_.make<EnumDef>(e->loc);
        n->base_type = expr(en->base_type);
        n->is_flags = en->is_flags;
        for (auto& m : en->members) n->members.push_back({m.name, expr(m.value), m.loc});
        return n;
      }
      case NK::PtrType: return ast_.make<PtrType>(e->loc, expr(as<PtrType>(e)->elem));
      case NK::ArrType: {
        auto* a = as<ArrType>(e);
        return ast_.make<ArrType>(e->loc, a->akind, expr(a->count), expr(a->elem));
      }
      case NK::ProcType: {
        auto* p = as<ProcTypeExpr>(e);
        auto* n = ast_.make<ProcTypeExpr>(e->loc);
        for (Expr* x : p->params) n->params.push_back(expr(x));
        for (Expr* x : p->returns) n->returns.push_back(expr(x));
        n->c_call = p->c_call;
        n->vararg = p->vararg;
        return n;
      }
      case NK::Ifx: {
        auto* x = as<Ifx>(e);
        return ast_.make<Ifx>(e->loc, expr(x->cond), expr(x->then_e), expr(x->else_e));
      }
      // Inserted by sema; a template is never checked, but copy what they wrap
      // so a clone of a checked tree is still a valid unchecked tree.
      case NK::ImplicitConv: return expr(as<ImplicitConv>(e)->operand);
      case NK::TupleGet: return expr(as<TupleGet>(e)->operand);
      case NK::Library: {
        auto* l = as<LibraryExpr>(e);
        return ast_.make<LibraryExpr>(e->loc, l->lib_name, l->system);
      }
      case NK::Directive: {
        auto* d = as<DirectiveExpr>(e);
        auto* n = ast_.make<DirectiveExpr>(e->loc, d->name, expr(d->operand));
        n->module_id = d->module_id;
        return n;
      }
      case NK::CodeLit: {
        auto* c = as<CodeLit>(e);
        auto* n = ast_.make<CodeLit>(e->loc);
        n->code_expr = expr(c->code_expr);
        n->code_block = block(c->code_block);
        return n;
      }
      default:
        fail(e->loc, "internal error: cannot clone expression node");
    }
  }

  Stmt* stmt(const Stmt* s) {
    if (!s) return nullptr;
    switch (s->kind) {
      case NK::Block: return block(as<Block>(s));
      case NK::ExprStmt: return ast_.make<ExprStmt>(s->loc, expr(as<ExprStmt>(s)->expr));
      case NK::Decl: return decl(as<Decl>(s));
      case NK::Assign: {
        auto* a = as<Assign>(s);
        auto* n = ast_.make<Assign>(s->loc, a->op);
        for (Expr* x : a->lhs) n->lhs.push_back(expr(x));
        for (Expr* x : a->rhs) n->rhs.push_back(expr(x));
        return n;
      }
      case NK::If: {
        auto* i = as<If>(s);
        return ast_.make<If>(s->loc, expr(i->cond), stmt(i->then_s), stmt(i->else_s));
      }
      case NK::Switch: {
        auto* sw = as<Switch>(s);
        auto* n = ast_.make<Switch>(s->loc, expr(sw->subject));
        n->complete = sw->complete;
        for (auto& c : sw->cases) {
          SwitchCase nc;
          for (Expr* v : c.values) nc.values.push_back(expr(v));
          nc.body = stmts(c.body);
          nc.fallthrough = c.fallthrough;
          nc.loc = c.loc;
          n->cases.push_back(std::move(nc));
        }
        return n;
      }
      case NK::While: {
        auto* w = as<While>(s);
        return ast_.make<While>(s->loc, expr(w->cond), stmt(w->body));
      }
      case NK::For: {
        auto* f = as<For>(s);
        auto* n = ast_.make<For>(s->loc);
        n->it_name = f->it_name;
        n->index_name = f->index_name;
        n->named = f->named;
        n->by_pointer = f->by_pointer;
        n->reverse = f->reverse;
        n->range_lo = expr(f->range_lo);
        n->range_hi = expr(f->range_hi);
        n->iterable = expr(f->iterable);
        n->body = stmt(f->body);
        return n;
      }
      case NK::Return: {
        auto* r = as<Return>(s);
        auto* n = ast_.make<Return>(s->loc);
        for (Expr* x : r->values) n->values.push_back(expr(x));
        n->backtick = r->backtick;
        return n;
      }
      case NK::Break:
      case NK::Continue:
        return ast_.make<BreakContinue>(s->kind, s->loc, as<BreakContinue>(s)->label);
      case NK::Defer: {
        auto* n = ast_.make<Defer>(s->loc, stmt(as<Defer>(s)->body));
        n->backtick = as<Defer>(s)->backtick;
        return n;
      }
      case NK::Insert: {
        auto* n = ast_.make<Insert>(s->loc, expr(as<Insert>(s)->operand));
        if (as<Insert>(s)->remove_code) n->remove_code = static_cast<Block*>(stmt(as<Insert>(s)->remove_code));
        return n;
      }
      case NK::Remove: return ast_.make<Remove>(s->loc, as<Remove>(s)->name);
      case NK::Import: {
        auto* i = as<Import>(s);
        auto* n = ast_.make<Import>(s->loc, i->module);
        n->module_id = i->module_id;
        return n;
      }
      case NK::Load: return ast_.make<Load>(s->loc, as<Load>(s)->path);
      case NK::StaticIf: {
        auto* si = as<StaticIf>(s);
        auto* n = ast_.make<StaticIf>(s->loc, expr(si->cond));
        n->then_s = stmts(si->then_s);
        n->else_s = stmts(si->else_s);
        return n;
      }
      case NK::Using: return ast_.make<UsingStmt>(s->loc, expr(as<UsingStmt>(s)->target));
      case NK::PushContext: {
        auto* pc = as<PushContext>(s);
        return ast_.make<PushContext>(s->loc, expr(pc->ctx), block(pc->body));
      }
      default:
        fail(s->loc, "internal error: cannot clone statement node");
    }
  }

 private:
  AstContext& ast_;

  template <typename T, typename N>
  static const T* as(const N* n) {
    return static_cast<const T*>(n);
  }

  std::vector<Arg> args(const std::vector<Arg>& in) {
    std::vector<Arg> out;
    for (auto& a : in) out.push_back(Arg{a.name, a.name_loc, expr(a.value), a.spread});
    return out;
  }

  std::vector<Stmt*> stmts(const std::vector<Stmt*>& in) {
    std::vector<Stmt*> out;
    for (Stmt* s : in) out.push_back(stmt(s));
    return out;
  }

  Block* block(const Block* b) {
    if (!b) return nullptr;
    auto* n = ast_.make<Block>(b->loc);
    n->stmts = stmts(b->stmts);
    return n;
  }

  Decl* decl(const Decl* d) {
    auto* n = ast_.make<Decl>(d->loc);
    n->names = d->names;
    n->type_expr = expr(d->type_expr);
    for (Expr* v : d->values) n->values.push_back(expr(v));
    n->is_const = d->is_const;
    n->uninit = d->uninit;
    n->is_using = d->is_using;
    n->is_vararg = d->is_vararg;
    n->is_baked = d->is_baked;
    n->auto_bake = d->auto_bake;
    n->is_global = d->is_global;
    n->backtick = d->backtick;
    n->add_context = d->add_context;
    n->insert = expr(d->insert);
    n->place_name = d->place_name;
    n->align = expr(d->align);
    return n;
  }

  ProcLit* proc(const ProcLit* p) {
    auto* n = ast_.make<ProcLit>(p->loc);
    for (Decl* d : p->params) n->params.push_back(decl(d));
    for (auto& r : p->returns) n->returns.push_back(ReturnSpec{r.name, expr(r.type_expr), r.loc});
    n->body = block(p->body);
    n->short_expr = expr(p->short_expr);
    n->is_foreign = p->is_foreign;
    n->foreign_lib = expr(p->foreign_lib);
    n->foreign_name = p->foreign_name;
    n->c_call = p->c_call;
    n->variadic = p->variadic;
    n->symmetric = p->symmetric;
    n->is_poly = p->is_poly;
    n->poly_returns = p->poly_returns;
    n->is_macro = p->is_macro;
    n->no_context = p->no_context;
    n->no_abc = p->no_abc;
    n->always_inline = p->always_inline;
    n->must = p->must;
    n->deprecated = p->deprecated;
    n->deprecated_note = p->deprecated_note;
    n->modify = block(p->modify);
    n->name = p->name;
    return n;
  }
};

}  // namespace

Expr* clone_expr(AstContext& ast, const Expr* e) { return Cloner(ast).expr(e); }
Stmt* clone_stmt(AstContext& ast, const Stmt* s) { return Cloner(ast).stmt(s); }

}  // namespace ojay
