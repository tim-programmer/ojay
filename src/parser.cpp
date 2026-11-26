#include "parser.h"

namespace ojay {

const char* binop_str(BinOp op) {
  switch (op) {
    case BinOp::Add: return "+";
    case BinOp::Sub: return "-";
    case BinOp::Mul: return "*";
    case BinOp::Div: return "/";
    case BinOp::Mod: return "%";
    case BinOp::BitAnd: return "&";
    case BinOp::BitOr: return "|";
    case BinOp::BitXor: return "^";
    case BinOp::Shl: return "<<";
    case BinOp::Shr: return ">>";
    case BinOp::Eq: return "==";
    case BinOp::Ne: return "!=";
    case BinOp::Lt: return "<";
    case BinOp::Le: return "<=";
    case BinOp::Gt: return ">";
    case BinOp::Ge: return ">=";
    case BinOp::LogAnd: return "&&";
    case BinOp::LogOr: return "||";
  }
  return "?";
}

namespace {

// Directives that may follow a procedure header and that we accept but ignore.
bool is_ignored_proc_directive(const std::string& d) {
  return d == "must" || d == "no_context" || d == "no_debug" || d == "no_abc" || d == "symmetric" ||
         d == "deprecated" || d == "cpp_method" || d == "no_aoc" || d == "dump" || d == "intrinsic" ||
         d == "runtime_support";
}

class Parser {
 public:
  Parser(AstContext& ast, const SourceManager& sm, uint32_t file)
      : ast_(ast), sm_(sm), toks_(lex(sm, file)) {}

  std::vector<Stmt*> parse_top_level() {
    std::vector<Stmt*> out;
    while (!at(Tok::Eof)) {
      if (accept(Tok::Semi)) continue;
      if (at(Tok::Directive) &&
          (cur().text == "scope_file" || cur().text == "scope_export" || cur().text == "scope_module")) {
        advance();
        continue;
      }
      out.push_back(parse_stmt(/*top_level=*/true));
    }
    return out;
  }

 private:
  AstContext& ast_;
  const SourceManager& sm_;
  std::vector<Token> toks_;
  size_t p_ = 0;
  int poly_idents_ = 0;  // `$T` identifiers parsed so far

  // `$T` (the cursor is at '$'): an identifier that introduces a type variable.
  Ident* parse_poly_ident() {
    SourceLoc loc = expect(Tok::Dollar).loc;
    if (at(Tok::Dollar)) fail(loc, "'$$' (auto-baking) is not supported yet");
    auto* id = ast_.make<Ident>(loc, expect_ident("type variable name after '$'"));
    id->poly = true;
    ++poly_idents_;
    return id;
  }

  // One parameter of a procedure or a polymorphic struct: [using] [$]name: T [= v] / name := v / name: ..T
  Decl* parse_param(ProcLit* p) {
    SourceLoc loc = cur().loc;
    auto* d = ast_.make<Decl>(loc);
    if (accept(Tok::KwUsing)) d->is_using = true;
    if (at(Tok::Dollar)) {
      advance();
      if (at(Tok::Dollar)) fail(loc, "'$$' (auto-baking) is not supported yet");
      d->is_baked = true;
    }
    d->names.push_back({expect_ident("parameter name"), loc});
    if (accept(Tok::ColonEq)) {
      d->values.push_back(parse_expr());
    } else {
      expect(Tok::Colon, "after parameter name");
      if (accept(Tok::DotDot)) {
        if (!p) fail(loc, "a struct parameter cannot be variadic");
        d->is_vararg = true;
        p->variadic = true;
      }
      d->type_expr = parse_type();
      if (accept(Tok::Eq)) d->values.push_back(parse_expr());
    }
    return d;
  }

  // ---- token helpers ----
  const Token& cur() const { return toks_[p_]; }
  const Token& peek(size_t o = 1) const { return toks_[std::min(p_ + o, toks_.size() - 1)]; }
  bool at(Tok k) const { return cur().kind == k; }
  bool at_directive(const char* name) const { return at(Tok::Directive) && cur().text == name; }
  const Token& advance() {
    const Token& t = toks_[p_];
    if (p_ + 1 < toks_.size()) ++p_;
    return t;
  }
  bool accept(Tok k) {
    if (at(k)) {
      advance();
      return true;
    }
    return false;
  }
  const Token& expect(Tok k, const char* what = nullptr) {
    if (!at(k)) {
      fail(cur().loc, "expected ", tok_name(k), what ? cat(" ", what) : std::string(), ", found ",
           describe(cur()));
    }
    return advance();
  }
  static std::string describe(const Token& t) {
    switch (t.kind) {
      case Tok::Ident: return "'" + t.text + "'";
      case Tok::Directive: return "'#" + t.text + "'";
      default: return tok_name(t.kind);
    }
  }
  std::string expect_ident(const char* what) {
    if (!at(Tok::Ident)) fail(cur().loc, "expected ", what, ", found ", describe(cur()));
    return advance().text;
  }

  // ---- statements ----

  Block* parse_block() {
    SourceLoc loc = expect(Tok::LBrace).loc;
    auto* b = ast_.make<Block>(loc);
    while (!at(Tok::RBrace)) {
      if (at(Tok::Eof)) fail(loc, "unterminated block (missing '}')");
      if (accept(Tok::Semi)) continue;
      b->stmts.push_back(parse_stmt(false));
    }
    advance();
    return b;
  }

  // Statement used as the body of if/while/for: a block or a single statement.
  Stmt* parse_body() {
    if (at(Tok::LBrace)) return parse_block();
    return parse_stmt(false);
  }

  Stmt* parse_stmt(bool top_level) {
    SourceLoc loc = cur().loc;
    switch (cur().kind) {
      case Tok::LBrace:
        if (top_level) fail(loc, "unexpected block at top level");
        return parse_block();
      case Tok::KwIf:
        return parse_if();
      case Tok::KwWhile: {
        advance();
        Expr* c = parse_expr();
        accept(Tok::KwThen);
        Stmt* body = parse_body();
        return ast_.make<While>(loc, c, body);
      }
      case Tok::KwFor:
        return parse_for();
      case Tok::KwReturn: {
        advance();
        auto* r = ast_.make<Return>(loc);
        if (!at(Tok::Semi)) r->values = parse_expr_list();
        expect(Tok::Semi, "after return statement");
        return r;
      }
      case Tok::KwBreak:
      case Tok::KwContinue: {
        NK k = at(Tok::KwBreak) ? NK::Break : NK::Continue;
        advance();
        std::string label;
        if (at(Tok::Ident)) label = advance().text;
        expect(Tok::Semi);
        return ast_.make<BreakContinue>(k, loc, label);
      }
      case Tok::KwDefer: {
        advance();
        Stmt* body = parse_body();
        return ast_.make<Defer>(loc, body);
      }
      case Tok::KwUsing: {
        if (top_level) fail(loc, "'using' is not supported at top level yet");
        advance();
        if (at(Tok::Ident) && (peek().kind == Tok::Colon || peek().kind == Tok::ColonEq)) {
          Stmt* s = parse_simple_stmt(false);  // using x := ...; / using x: T;
          auto* d = static_cast<Decl*>(s);
          if (d->is_const) fail(loc, "'using' needs a variable, not a constant");
          d->is_using = true;
          return d;
        }
        Expr* target = parse_expr();
        expect(Tok::Semi, "after 'using'");
        return ast_.make<UsingStmt>(loc, target);
      }
      case Tok::Directive:
        return parse_directive_stmt(top_level);
      case Tok::KwRemove: {  // `remove it;` in a for loop
        if (top_level) fail(loc, "'remove' is only allowed in a for loop");
        advance();
        std::string name = expect(Tok::Ident, "after 'remove' (the iterator to remove, e.g. remove it;)").text;
        expect(Tok::Semi, "after remove statement");
        return ast_.make<Remove>(loc, name);
      }
      case Tok::Ident:
        // `push_context ctx { ... }`. push_context is not a reserved word: it
        // starts the statement when an expression follows it.
        if (cur().text == "push_context") {
          Tok n = peek().kind;
          if (n == Tok::Ident || n == Tok::Backtick || n == Tok::DotLBrace || n == Tok::Directive ||
              n == Tok::LBrace || n == Tok::Star || n == Tok::Shl || n == Tok::LParen || n == Tok::KwCast ||
              n == Tok::KwXx || n == Tok::KwNull || n == Tok::Int || n == Tok::String || n == Tok::KwIfx) {
            if (top_level) fail(loc, "push_context is only allowed inside a procedure");
            advance();
            if (at(Tok::LBrace)) fail(cur().loc, "push_context needs a context: push_context ctx { ... }");
            Expr* ctx = parse_expr();
            if (!at(Tok::LBrace)) fail(cur().loc, "expected '{' after push_context ", "expression, found ", describe(cur()));
            Block* body = parse_block();
            return ast_.make<PushContext>(loc, ctx, body);
          }
        }
        break;
      case Tok::Backtick:
        if (top_level) fail(loc, "backtick names (`name) are only allowed in the body of an #expand macro");
        if (peek().kind == Tok::KwReturn || peek().kind == Tok::KwDefer) {
          advance();
          Stmt* st = parse_stmt(false);
          if (st->kind == NK::Return) static_cast<Return*>(st)->backtick = true;
          else static_cast<Defer*>(st)->backtick = true;
          st->loc = loc;
          return st;
        }
        if (peek().kind == Tok::KwBreak || peek().kind == Tok::KwContinue)
          fail(loc, "`", peek().kind == Tok::KwBreak ? "break" : "continue", " is not supported yet");
        if (peek().kind == Tok::Ident &&
            (peek(2).kind == Tok::Colon || peek(2).kind == Tok::ColonColon || peek(2).kind == Tok::ColonEq)) {
          advance();  // `x := ...; declares x in the macro caller's scope
          auto* d = ast_.make<Decl>(loc);
          d->names.push_back({cur().text, cur().loc});
          advance();
          d->backtick = true;
          parse_decl_rest(d);
          return d;
        }
        break;
      default:
        break;
    }
    return parse_simple_stmt(top_level);
  }

  Stmt* parse_directive_stmt(bool top_level) {
    SourceLoc loc = cur().loc;
    const std::string d = cur().text;
    if (d == "import") {
      advance();
      skip_directive_modifiers();
      std::string name = expect(Tok::String, "module name").text;
      expect(Tok::Semi);
      return ast_.make<Import>(loc, name);
    }
    if (d == "load") {
      advance();
      std::string path = expect(Tok::String, "file path").text;
      expect(Tok::Semi);
      return ast_.make<Load>(loc, path);
    }
    if (d == "if") {
      advance();
      return parse_static_if(loc, top_level);
    }
    if (d == "assert") {
      advance();
      Expr* c = parse_expr();
      if (accept(Tok::Comma)) parse_expr();  // optional message
      expect(Tok::Semi);
      return ast_.make<ExprStmt>(loc, ast_.make<DirectiveExpr>(loc, "assert", c));
    }
    if (d == "run") {
      // `#run expr;`: executed during compilation for its side effects.
      advance();
      reject_run_modifiers();
      Expr* e = parse_expr();
      expect(Tok::Semi, "after #run statement");
      return ast_.make<ExprStmt>(loc, ast_.make<DirectiveExpr>(loc, "run", e));
    }
    if (d == "insert") {
      // `#insert expr;`: expr is a Code or string constant.
      advance();
      reject_insert_modifiers();
      // `#insert (remove = { ... }) body;` (for_expansion)
      Block* remove_code = nullptr;
      if (at(Tok::LParen) && (peek().kind == Tok::Ident || peek().kind == Tok::KwRemove) && peek(2).kind == Tok::Eq) {
        advance();
        while (!at(Tok::RParen)) {
          Token name = advance();
          expect(Tok::Eq);
          if (name.kind != Tok::KwRemove)
            fail(name.loc, "#insert (", name.text, " = ...) is not supported (only remove = { ... })");
          if (!at(Tok::LBrace)) fail(cur().loc, "expected a block after 'remove =', found ", describe(cur()));
          remove_code = parse_block();
          if (!accept(Tok::Comma)) break;
        }
        expect(Tok::RParen);
      }
      Expr* e = parse_expr();
      expect(Tok::Semi, "after #insert statement");
      auto* ins = ast_.make<Insert>(loc, e);
      ins->remove_code = remove_code;
      return ins;
    }
    if (d == "add_context") {
      // `#add_context name: T = value;`: a member added to the Context struct.
      if (!top_level) fail(loc, "#add_context is only allowed at top level");
      advance();
      if (!at(Tok::Ident) || (peek().kind != Tok::Colon && peek().kind != Tok::ColonEq))
        fail(cur().loc, "#add_context needs a member declaration: #add_context name: Type = value;");
      Stmt* st = parse_simple_stmt(true);
      auto* decl = static_cast<Decl*>(st);
      if (decl->is_const) fail(loc, "#add_context needs a variable member, not a constant");
      decl->add_context = true;
      decl->loc = loc;
      return decl;
    }
    if (d == "through") fail(loc, "#through is only allowed at the end of an 'if ==' case");
    return parse_simple_stmt(top_level);
  }

  // `#run,stallable` / `#run,host`: need the compiler message loop.
  void reject_run_modifiers() {
    if (at(Tok::Comma))
      fail(cur().loc, "#run,", peek().kind == Tok::Ident ? peek().text : "...", " is not supported yet");
  }

  // `#insert,scope(...)` / `#insert -> string { ... }`
  void reject_insert_modifiers() {
    if (at(Tok::Comma))
      fail(cur().loc, "#insert,", peek().kind == Tok::Ident ? peek().text : "...", " is not supported yet");
    if (at(Tok::Arrow)) fail(cur().loc, "#insert -> string { ... } is not supported yet; use #insert #run");
  }

  void skip_directive_modifiers() {
    // #import,dir "..." / #import,file "..."  (we only support module names)
    while (accept(Tok::Comma)) expect_ident("directive modifier");
  }

  StaticIf* parse_static_if(SourceLoc loc, bool top_level) {
    auto* s = ast_.make<StaticIf>(loc, parse_expr());
    auto parse_branch = [&](std::vector<Stmt*>& out) {
      if (at(Tok::LBrace)) {
        advance();
        while (!at(Tok::RBrace)) {
          if (at(Tok::Eof)) fail(loc, "unterminated #if block");
          if (accept(Tok::Semi)) continue;
          out.push_back(parse_stmt(top_level));
        }
        advance();
      } else {
        out.push_back(parse_stmt(top_level));
      }
    };
    accept(Tok::KwThen);
    parse_branch(s->then_s);
    if (accept(Tok::KwElse)) {
      if (at_directive("if")) {
        SourceLoc l2 = advance().loc;
        s->else_s.push_back(parse_static_if(l2, top_level));
      } else {
        parse_branch(s->else_s);
      }
    }
    return s;
  }

  Stmt* parse_if() {
    SourceLoc loc = expect(Tok::KwIf).loc;
    bool complete = false;
    if (at_directive("complete")) {
      advance();
      complete = true;
    }
    Expr* cond = parse_expr();
    if (at(Tok::EqEq) && peek().kind == Tok::LBrace) {
      advance();
      advance();
      auto* sw = ast_.make<Switch>(loc, cond);
      sw->complete = complete;
      while (!at(Tok::RBrace)) {
        if (at(Tok::Semi)) {
          advance();
          continue;
        }
        if (!at(Tok::KwCase)) fail(cur().loc, "expected 'case' inside 'if ==' block, found ", describe(cur()));
        SwitchCase c;
        c.loc = advance().loc;
        if (!at(Tok::Semi)) c.values = parse_expr_list();
        expect(Tok::Semi, "after case value");
        while (!at(Tok::KwCase) && !at(Tok::RBrace)) {
          if (at(Tok::Eof)) fail(loc, "unterminated 'if ==' block");
          if (accept(Tok::Semi)) continue;
          if (at_directive("through")) {
            advance();
            expect(Tok::Semi);
            c.fallthrough = true;
            if (!at(Tok::KwCase)) fail(cur().loc, "#through must be the last statement of a case");
            continue;
          }
          c.body.push_back(parse_stmt(false));
        }
        sw->cases.push_back(std::move(c));
      }
      advance();
      return sw;
    }
    accept(Tok::KwThen);
    Stmt* then_s = parse_body();
    Stmt* else_s = nullptr;
    if (accept(Tok::KwElse)) else_s = parse_body();
    return ast_.make<If>(loc, cond, then_s, else_s);
  }

  Stmt* parse_for() {
    SourceLoc loc = expect(Tok::KwFor).loc;
    auto* f = ast_.make<For>(loc);
    for (;;) {
      if (accept(Tok::Lt)) f->reverse = true;
      else if (accept(Tok::Star)) f->by_pointer = true;
      else if (accept(Tok::Shl)) {  // `for <<` is not a thing, but `<*` lexes fine; guard anyway
        fail(loc, "unexpected '<<' after 'for'");
      } else break;
    }
    if (at(Tok::Ident) && peek().kind == Tok::Colon) {
      f->it_name = advance().text;
      advance();
      f->named = true;
    } else if (at(Tok::Ident) && peek().kind == Tok::Comma && peek(2).kind == Tok::Ident &&
               peek(3).kind == Tok::Colon) {
      f->it_name = advance().text;
      advance();
      f->index_name = advance().text;
      advance();
      f->named = true;
    }
    Expr* e = parse_expr();
    if (accept(Tok::DotDot)) {
      f->range_lo = e;
      f->range_hi = parse_expr();
    } else {
      f->iterable = e;
    }
    f->body = parse_body();
    return f;
  }

  bool is_assign_op(Tok k) const {
    switch (k) {
      case Tok::Eq: case Tok::PlusEq: case Tok::MinusEq: case Tok::StarEq: case Tok::SlashEq:
      case Tok::PercentEq: case Tok::AmpEq: case Tok::PipeEq: case Tok::CaretEq: case Tok::ShlEq:
      case Tok::ShrEq:
        return true;
      default:
        return false;
    }
  }

  // `operator + :: ...`: the operator's name ("operator+") when the cursor is
  // at one, else empty. Advances past the operator symbol.
  std::string parse_operator_name() {
    if (!(at(Tok::Ident) && cur().text == "operator")) return "";
    size_t n = 0;
    std::string sym;
    auto tok_text = [](Tok k) -> const char* {
      switch (k) {
        case Tok::Plus: return "+"; case Tok::Minus: return "-"; case Tok::Star: return "*";
        case Tok::Slash: return "/"; case Tok::Percent: return "%"; case Tok::Amp: return "&";
        case Tok::Pipe: return "|"; case Tok::Caret: return "^"; case Tok::Shl: return "<<";
        case Tok::Shr: return ">>"; case Tok::EqEq: return "=="; case Tok::NotEq: return "!=";
        case Tok::Lt: return "<"; case Tok::LtEq: return "<="; case Tok::Gt: return ">";
        case Tok::GtEq: return ">="; case Tok::PlusEq: return "+="; case Tok::MinusEq: return "-=";
        case Tok::StarEq: return "*="; case Tok::SlashEq: return "/="; case Tok::PercentEq: return "%=";
        case Tok::AmpEq: return "&="; case Tok::PipeEq: return "|="; case Tok::CaretEq: return "^=";
        case Tok::ShlEq: return "<<="; case Tok::ShrEq: return ">>=";
        default: return nullptr;
      }
    };
    if (peek(1).kind == Tok::Star && peek(2).kind == Tok::LBracket && peek(3).kind == Tok::RBracket) {
      sym = "*[]";
      n = 3;
    } else if (peek(1).kind == Tok::LBracket && peek(2).kind == Tok::RBracket) {
      sym = peek(3).kind == Tok::Eq ? "[]=" : "[]";
      n = peek(3).kind == Tok::Eq ? 3 : 2;
    } else if (const char* t = tok_text(peek(1).kind)) {
      sym = t;
      n = 1;
    } else {
      return "";
    }
    if (peek(n + 1).kind != Tok::ColonColon) return "";
    advance();
    for (size_t i = 0; i < n; ++i) advance();
    return "operator" + sym;
  }

  // Declarations, assignments and expression statements.
  Stmt* parse_simple_stmt(bool top_level) {
    SourceLoc loc = cur().loc;
    {
      std::string op = parse_operator_name();
      if (!op.empty()) {
        auto* d = ast_.make<Decl>(loc);
        d->names.push_back({op, loc});
        d->is_global = top_level;
        parse_decl_rest(d);
        if (d->values.size() != 1 || d->values[0]->kind != NK::ProcLit)
          fail(loc, "'", op, "' must be a procedure");
        return d;
      }
    }
    // Look for `name, name, ... :`.
    {
      size_t i = 0;
      bool ok = false;
      while (peek(i).kind == Tok::Ident) {
        Tok next = peek(i + 1).kind;
        if (next == Tok::Colon || next == Tok::ColonColon || next == Tok::ColonEq) {
          ok = true;
          break;
        }
        if (next != Tok::Comma) break;
        i += 2;
      }
      if (ok) {
        auto* d = ast_.make<Decl>(loc);
        for (;;) {
          DeclName n{cur().text, cur().loc};
          advance();
          d->names.push_back(n);
          if (!accept(Tok::Comma)) break;
        }
        d->is_global = top_level;
        parse_decl_rest(d);
        return d;
      }
    }
    if (top_level) fail(loc, "expected a declaration at top level, found ", describe(cur()));

    std::vector<Expr*> lhs = parse_expr_list();
    if (is_assign_op(cur().kind)) {
      auto* a = ast_.make<Assign>(cur().loc, cur().kind);
      advance();
      a->lhs = std::move(lhs);
      a->rhs = parse_expr_list();
      expect(Tok::Semi, "after assignment");
      return a;
    }
    if (lhs.size() != 1) fail(loc, "expected '=' or ':' after expression list");
    expect(Tok::Semi, "after expression");
    return ast_.make<ExprStmt>(loc, lhs[0]);
  }

  static bool ends_with_block(Expr* e) {
    if (!e) return false;
    if (e->kind == NK::ProcLit) return static_cast<ProcLit*>(e)->body != nullptr;
    // A here-string ends with its terminator line; a ';' after it is optional.
    if (e->kind == NK::StringLit) return static_cast<StringLit*>(e)->here_string;
    if (e->kind == NK::CodeLit) return static_cast<CodeLit*>(e)->code_block != nullptr;
    return e->kind == NK::StructDef || e->kind == NK::EnumDef;
  }

  // `member` (a struct member): `#align N` may follow the type or the value.
  void parse_decl_rest(Decl* d, bool member = false) {
    (void)member;
    auto align = [&] {
      if (!at_directive("align")) return;
      SourceLoc loc = advance().loc;
      if (d->align) fail(loc, "a member can only have one #align");
      d->align = parse_unary();
    };
    if (accept(Tok::ColonColon)) {
      d->is_const = true;
      d->values = parse_expr_list();
      align();  // rejected below
    } else if (accept(Tok::ColonEq)) {
      d->values = parse_expr_list();
      align();
    } else {
      expect(Tok::Colon);
      d->type_expr = parse_type();
      align();
      if (accept(Tok::Eq)) {
        if (accept(Tok::TripleDash)) d->uninit = true;
        else d->values = parse_expr_list();
      } else if (accept(Tok::Colon)) {
        d->is_const = true;
        d->values = parse_expr_list();
      }
      if (!d->is_const && !d->uninit && !d->values.empty()) align();
    }
    if (at_directive("align")) fail(cur().loc, "#align must follow the type or the value");
    if (d->is_const && d->align) fail(d->align->loc, "#align is not allowed on a constant");
    if (!d->values.empty() && ends_with_block(d->values.back())) {
      accept(Tok::Semi);
    } else {
      expect(Tok::Semi, "after declaration");
    }
    for (Expr* v : d->values) {
      if (v->kind == NK::ProcLit && d->names.size() == 1) static_cast<ProcLit*>(v)->name = d->names[0].name;
    }
  }

  // ---- expressions ----

  std::vector<Expr*> parse_expr_list() {
    std::vector<Expr*> v;
    v.push_back(parse_expr());
    while (accept(Tok::Comma)) v.push_back(parse_expr());
    return v;
  }

  Expr* parse_expr() { return parse_binary(1); }

  static int precedence(Tok k, BinOp& op) {
    switch (k) {
      case Tok::OrOr: op = BinOp::LogOr; return 1;
      case Tok::AndAnd: op = BinOp::LogAnd; return 2;
      case Tok::EqEq: op = BinOp::Eq; return 3;
      case Tok::NotEq: op = BinOp::Ne; return 3;
      case Tok::Lt: op = BinOp::Lt; return 3;
      case Tok::LtEq: op = BinOp::Le; return 3;
      case Tok::Gt: op = BinOp::Gt; return 3;
      case Tok::GtEq: op = BinOp::Ge; return 3;
      case Tok::Pipe: op = BinOp::BitOr; return 4;
      case Tok::Caret: op = BinOp::BitXor; return 5;
      case Tok::Amp: op = BinOp::BitAnd; return 6;
      case Tok::Shl: op = BinOp::Shl; return 7;
      case Tok::Shr: op = BinOp::Shr; return 7;
      case Tok::Plus: op = BinOp::Add; return 8;
      case Tok::Minus: op = BinOp::Sub; return 8;
      case Tok::Star: op = BinOp::Mul; return 9;
      case Tok::Slash: op = BinOp::Div; return 9;
      case Tok::Percent: op = BinOp::Mod; return 9;
      default: return 0;
    }
  }

  Expr* parse_binary(int min_prec) {
    Expr* lhs = parse_unary();
    for (;;) {
      BinOp op;
      int prec = precedence(cur().kind, op);
      if (prec == 0 || prec < min_prec) return lhs;
      // `if x == {` starts a switch; leave the `==` for parse_if.
      if (cur().kind == Tok::EqEq && peek().kind == Tok::LBrace) return lhs;
      SourceLoc loc = advance().loc;
      Expr* rhs = parse_binary(prec + 1);
      lhs = ast_.make<Binary>(loc, op, lhs, rhs);
    }
  }

  Expr* parse_unary() {
    SourceLoc loc = cur().loc;
    switch (cur().kind) {
      case Tok::Minus: advance(); return ast_.make<Unary>(loc, UnOp::Neg, parse_unary());
      case Tok::Plus: advance(); return ast_.make<Unary>(loc, UnOp::Plus, parse_unary());
      case Tok::Bang: advance(); return ast_.make<Unary>(loc, UnOp::Not, parse_unary());
      case Tok::Tilde: advance(); return ast_.make<Unary>(loc, UnOp::BitNot, parse_unary());
      case Tok::Star: advance(); return ast_.make<Unary>(loc, UnOp::AddrOf, parse_unary());
      case Tok::Shl: advance(); return ast_.make<Unary>(loc, UnOp::Deref, parse_unary());
      case Tok::KwXx: advance(); return ast_.make<AutoCast>(loc, parse_unary());
      case Tok::KwCast: {
        advance();
        bool trunc = false, no_check = false;
        // cast,trunc(T) / cast,no_check(T)
        while (accept(Tok::Comma)) {
          std::string m = expect_ident("cast modifier");
          if (m == "trunc") trunc = true;
          else if (m == "no_check") no_check = true;
          else fail(loc, "unknown cast modifier '", m, "'");
        }
        expect(Tok::LParen, "after 'cast'");
        Expr* t = parse_type();
        expect(Tok::RParen, "after cast type");
        auto* c = ast_.make<Cast>(loc, t, parse_unary());
        c->truncate = trunc;
        c->no_check = no_check;
        return c;
      }
      case Tok::KwInline:
        advance();  // call-site `inline` is a hint we ignore
        return parse_unary();
      default:
        return parse_postfix(parse_primary());
    }
  }

  Expr* parse_postfix(Expr* e) {
    for (;;) {
      SourceLoc loc = cur().loc;
      switch (cur().kind) {
        case Tok::LParen: {
          advance();
          auto* c = ast_.make<Call>(loc, e);
          c->loc = e->loc;
          parse_args(c->args, Tok::RParen);
          e = c;
          break;
        }
        case Tok::LBracket: {
          advance();
          Expr* idx = parse_expr();
          expect(Tok::RBracket);
          e = ast_.make<Index>(loc, e, idx);
          break;
        }
        case Tok::Dot: {
          advance();
          std::string name = expect_ident("member name after '.'");
          e = ast_.make<Member>(loc, e, name);
          break;
        }
        case Tok::DotStar:
          advance();
          e = ast_.make<Unary>(loc, UnOp::Deref, e);
          break;
        case Tok::DotLBrace: {
          advance();
          auto* s = ast_.make<StructLit>(e->loc, e);
          parse_args(s->args, Tok::RBrace);
          e = s;
          break;
        }
        case Tok::DotLBracket: {
          advance();
          auto* a = ast_.make<ArrayLit>(e->loc, e);
          parse_array_elems(a);
          e = a;
          break;
        }
        default:
          return e;
      }
    }
  }

  void parse_args(std::vector<Arg>& args, Tok close) {
    while (!at(close)) {
      Arg a;
      if (at(Tok::Ident) && peek().kind == Tok::Eq) {
        a.name = cur().text;
        a.name_loc = cur().loc;
        advance();
        advance();
      }
      if (close == Tok::RParen && at(Tok::DotDot)) {  // `f(..array)`
        advance();
        a.spread = true;
      }
      a.value = parse_expr();
      args.push_back(a);
      if (!accept(Tok::Comma)) break;
    }
    expect(close);
  }

  void parse_array_elems(ArrayLit* a) {
    while (!at(Tok::RBracket)) {
      a->elems.push_back(parse_expr());
      if (!accept(Tok::Comma)) break;
    }
    expect(Tok::RBracket);
  }

  // Decides whether the '(' at the cursor starts a procedure literal.
  bool looks_like_proc_lit() const {
    size_t i = 0;
    int depth = 0;
    // find matching paren
    for (;; ++i) {
      Tok k = peek(i).kind;
      if (k == Tok::Eof) return false;
      if (k == Tok::LParen) ++depth;
      else if (k == Tok::RParen && --depth == 0) break;
    }
    Tok after = peek(i + 1).kind;
    if (i == 1) {
      // `()` followed by a body, return type or directive
      return after == Tok::LBrace || after == Tok::Arrow || after == Tok::Directive;
    }
    Tok first = peek(1).kind;
    if (first == Tok::Dollar || first == Tok::KwUsing) return true;
    if (first == Tok::Ident) {
      Tok second = peek(2).kind;
      if (second == Tok::Colon || second == Tok::ColonEq) return true;
      // `(a, b: int)`
      if (second == Tok::Comma) {
        size_t j = 1;
        while (peek(j).kind == Tok::Ident && peek(j + 1).kind == Tok::Comma) j += 2;
        return peek(j).kind == Tok::Ident && peek(j + 1).kind == Tok::Colon;
      }
    }
    return false;
  }

  Expr* parse_primary() {
    const Token& t = cur();
    SourceLoc loc = t.loc;
    switch (t.kind) {
      case Tok::Int: {
        auto v = t.ival;
        advance();
        return ast_.make<IntLit>(loc, v);
      }
      case Tok::Float: {
        double v = t.fval;
        advance();
        return ast_.make<FloatLit>(loc, v);
      }
      case Tok::String: {
        std::string s = t.text;
        advance();
        return ast_.make<StringLit>(loc, s);
      }
      case Tok::KwTrue: advance(); return ast_.make<BoolLit>(loc, true);
      case Tok::KwFalse: advance(); return ast_.make<BoolLit>(loc, false);
      case Tok::KwNull: advance(); return ast_.make<NullLit>(loc);
      case Tok::Ident: {
        std::string n = t.text;
        advance();
        return ast_.make<Ident>(loc, n);
      }
      case Tok::LParen: {
        if (looks_like_proc_lit()) return parse_proc_lit();
        advance();
        Expr* e = parse_expr();
        expect(Tok::RParen);
        return e;
      }
      case Tok::DotLBrace: {
        advance();
        auto* s = ast_.make<StructLit>(loc, nullptr);
        parse_args(s->args, Tok::RBrace);
        return s;
      }
      case Tok::DotLBracket: {
        advance();
        auto* a = ast_.make<ArrayLit>(loc, nullptr);
        parse_array_elems(a);
        return a;
      }
      case Tok::Dot: {
        advance();
        std::string name = expect_ident("enum member name after '.'");
        return ast_.make<Member>(loc, nullptr, name);
      }
      case Tok::LBracket:
        return parse_type();
      case Tok::KwStruct:
      case Tok::KwUnion:
        return parse_struct();
      case Tok::KwEnum:
      case Tok::KwEnumFlags:
        return parse_enum();
      case Tok::KwIfx: {
        advance();
        Expr* c = parse_expr();
        accept(Tok::KwThen);
        Expr* a = parse_expr();
        expect(Tok::KwElse, "in 'ifx' expression");
        Expr* b = parse_expr();
        return ast_.make<Ifx>(loc, c, a, b);
      }
      case Tok::Dollar:
        return parse_poly_ident();
      case Tok::Backtick: {
        advance();
        auto* id = ast_.make<Ident>(loc, expect_ident("name after '`'"));
        id->backtick = true;
        return id;
      }
      case Tok::Directive:
        return parse_directive_expr();
      default:
        fail(loc, "expected an expression, found ", describe(t));
    }
  }

  Expr* parse_directive_expr() {
    SourceLoc loc = cur().loc;
    std::string d = advance().text;
    if (d == "char") {
      const Token& s = expect(Tok::String, "after #char");
      if (s.text.size() != 1) fail(loc, "#char expects a single-character string");
      return ast_.make<IntLit>(loc, (unsigned char)s.text[0]);
    }
    if (d == "string") {
      std::string s = expect(Tok::String).text;
      auto* lit = ast_.make<StringLit>(loc, s);
      lit->here_string = true;
      return lit;
    }
    if (d == "type") return parse_type_directive(loc);
    if (d == "this") return ast_.make<DirectiveExpr>(loc, "this", nullptr);
    if (d == "system_library" || d == "library") {
      skip_directive_modifiers();
      std::string n = expect(Tok::String, "library name").text;
      return ast_.make<LibraryExpr>(loc, n, d == "system_library");
    }
    if (d == "line") return ast_.make<IntLit>(loc, loc.line);
    if (d == "file") return ast_.make<StringLit>(loc, sm_.get(loc.file).path);
    if (d == "import") {
      skip_directive_modifiers();
      std::string n = expect(Tok::String, "module name").text;
      return ast_.make<DirectiveExpr>(loc, "import:" + n, nullptr);
    }
    if (d == "run") {
      reject_run_modifiers();
      return ast_.make<DirectiveExpr>(loc, "run", parse_unary());
    }
    if (d == "code") {
      auto* c = ast_.make<CodeLit>(loc);
      if (at(Tok::Comma)) fail(cur().loc, "#code,", peek().kind == Tok::Ident ? peek().text : "...",
                               " is not supported yet");
      if (at(Tok::LBrace)) c->code_block = parse_block();
      else c->code_expr = parse_expr();
      return c;
    }
    if (d == "insert") {
      reject_insert_modifiers();
      return ast_.make<DirectiveExpr>(loc, "insert", parse_unary());
    }
    if (d == "caller_location") return ast_.make<DirectiveExpr>(loc, d, nullptr);
    if (d == "location") {
      expect(Tok::LParen, "after #location");
      Expr* x = parse_expr();
      expect(Tok::RParen, "after #location's operand");
      return ast_.make<DirectiveExpr>(loc, d, x);
    }
    if (d == "procedure_of_call" || d == "bake_arguments" || d == "compiler") {
      fail(loc, "#", d, " is not supported yet");
    }
    if (d == "modify" || d == "expand") fail(loc, "#", d, " must follow a procedure's parameter list");
    fail(loc, "unknown directive '#", d, "' in expression");
  }

  // ---- types ----

  Expr* parse_type() {
    SourceLoc loc = cur().loc;
    switch (cur().kind) {
      case Tok::Star:
        advance();
        return ast_.make<PtrType>(loc, parse_type());
      case Tok::LBracket: {
        advance();
        if (accept(Tok::RBracket)) return ast_.make<ArrType>(loc, ArrayKind::View, nullptr, parse_type());
        if (at(Tok::DotDot) && peek().kind == Tok::RBracket) {
          advance();
          advance();
          return ast_.make<ArrType>(loc, ArrayKind::Dynamic, nullptr, parse_type());
        }
        Expr* n = parse_expr();
        expect(Tok::RBracket, "after array size");
        return ast_.make<ArrType>(loc, ArrayKind::Fixed, n, parse_type());
      }
      case Tok::LParen:
        return parse_proc_type();
      case Tok::Directive:
        if (cur().text == "type") {
          advance();
          return parse_type_directive(loc);
        }
        if (cur().text == "this") {
          advance();
          return ast_.make<DirectiveExpr>(loc, "this", nullptr);
        }
        fail(loc, "unexpected directive '#", cur().text, "' in type");
      case Tok::Dollar: {
        Ident* id = parse_poly_ident();
        if (at(Tok::Slash))
          fail(cur().loc, "type constraints ($", id->name, "/Interface) are not supported yet");
        return id;
      }
      case Tok::KwStruct:
      case Tok::KwUnion:
        return parse_struct();
      case Tok::KwEnum:
      case Tok::KwEnumFlags:
        return parse_enum();
      case Tok::Ident: {
        Expr* e = ast_.make<Ident>(loc, advance().text);
        // Module-qualified names (Module.Type)
        while (at(Tok::Dot) && peek().kind == Tok::Ident) {
          SourceLoc l2 = advance().loc;
          e = ast_.make<Member>(l2, e, advance().text);
        }
        // A polymorphic struct instance: Vector(float, 3)
        if (at(Tok::LParen)) {
          advance();
          auto* c = ast_.make<Call>(e->loc, e);
          parse_args(c->args, Tok::RParen);
          return c;
        }
        return e;
      }
      default:
        fail(loc, "expected a type, found ", describe(cur()));
    }
  }

  // After `#type`: a type, or `#type,distinct T` (a distinct type).
  Expr* parse_type_directive(SourceLoc loc) {
    if (accept(Tok::Comma)) {
      std::string m = at(Tok::Ident) ? cur().text : "";
      if (m == "distinct" || m == "isa") {
        advance();
        return ast_.make<DirectiveExpr>(loc, m, parse_type());
      }
      fail(cur().loc, "unknown #type modifier ", describe(cur()), " (expected 'distinct' or 'isa')");
    }
    return parse_type();
  }

  Expr* parse_proc_type() {
    auto* pt = ast_.make<ProcTypeExpr>(expect(Tok::LParen).loc);
    while (!at(Tok::RParen)) {
      // Optional parameter name.
      if (at(Tok::Ident) && peek().kind == Tok::Colon) {
        advance();
        advance();
      }
      if (at(Tok::DotDot)) {
        if (pt->vararg >= 0) fail(cur().loc, "a procedure type can have only one variadic parameter");
        advance();
        pt->vararg = (int)pt->params.size();
        pt->params.push_back(ast_.make<ArrType>(cur().loc, ArrayKind::View, nullptr, parse_type()));
      } else {
        pt->params.push_back(parse_type());
      }
      if (!accept(Tok::Comma)) break;
    }
    expect(Tok::RParen);
    if (accept(Tok::Arrow)) {
      if (at(Tok::LParen) && !looks_like_proc_type_return()) {
        advance();
        while (!at(Tok::RParen)) {
          if (at(Tok::Ident) && peek().kind == Tok::Colon) {
            advance();
            advance();
          }
          pt->returns.push_back(parse_type());
          if (!accept(Tok::Comma)) break;
        }
        expect(Tok::RParen);
      } else {
        pt->returns.push_back(parse_type());
      }
    }
    while (at(Tok::Directive)) {
      if (cur().text == "c_call") pt->c_call = true;
      else if (!is_ignored_proc_directive(cur().text)) break;
      advance();
    }
    return pt;
  }

  // In `-> (...)`, distinguishes a parenthesized return list from a procedure type
  // being returned: `-> (int) -> int` is a proc type.
  bool looks_like_proc_type_return() const {
    size_t i = 0;
    int depth = 0;
    for (;; ++i) {
      Tok k = peek(i).kind;
      if (k == Tok::Eof) return false;
      if (k == Tok::LParen) ++depth;
      else if (k == Tok::RParen && --depth == 0) break;
    }
    return peek(i + 1).kind == Tok::Arrow;
  }

  ProcLit* parse_proc_lit() {
    auto* p = ast_.make<ProcLit>(expect(Tok::LParen).loc);
    int polys = poly_idents_;
    while (!at(Tok::RParen)) {
      Decl* d = parse_param(p);
      if (d->is_baked) p->is_poly = true;
      p->params.push_back(d);
      if (!accept(Tok::Comma)) break;
    }
    expect(Tok::RParen);
    if (poly_idents_ != polys) p->is_poly = true;
    polys = poly_idents_;
    if (accept(Tok::Arrow)) parse_returns(p);
    if (poly_idents_ != polys) p->poly_returns = true;
    // Trailing directives.
    while (at(Tok::Directive)) {
      const std::string d = cur().text;
      SourceLoc loc = advance().loc;
      if (d == "foreign") {
        p->is_foreign = true;
        p->c_call = true;
        if (at(Tok::Ident)) p->foreign_lib = ast_.make<Ident>(cur().loc, advance().text);
        if (at(Tok::String)) p->foreign_name = advance().text;
      } else if (d == "c_call") {
        p->c_call = true;
      } else if (d == "no_context") {
        p->no_context = true;
      } else if (d == "program_export") {
        p->program_export = true;
        if (at(Tok::String)) p->export_name = advance().text;
      } else if (d == "symmetric") {
        p->symmetric = true;
      } else if (d == "deprecated") {
        if (at(Tok::String)) advance();
      } else if (d == "expand") {
        p->is_macro = true;
      } else if (d == "modify") {
        if (p->modify) fail(loc, "a procedure can only have one #modify block");
        if (!at(Tok::LBrace)) fail(cur().loc, "expected '{' after #modify, found ", describe(cur()));
        p->modify = parse_block();
      } else if (d == "compiler" || d == "elsewhere") {
        fail(loc, "#", d, " procedures are not supported yet");
      } else if (!is_ignored_proc_directive(d)) {
        fail(loc, "unknown procedure directive '#", d, "'");
      }
    }
    if (p->is_macro && p->is_foreign) fail(p->loc, "an #expand macro cannot be #foreign");
    if (!p->is_foreign) {
      if (!at(Tok::LBrace)) fail(cur().loc, "expected procedure body, found ", describe(cur()));
      p->body = parse_block();
    }
    return p;
  }

  void parse_returns(ProcLit* p) {
    // Named or parenthesized returns: -> (a: int, b: float)
    if (at(Tok::LParen) && !looks_like_proc_type_return()) {
      advance();
      while (!at(Tok::RParen)) {
        ReturnSpec r;
        r.loc = cur().loc;
        if (at(Tok::Ident) && peek().kind == Tok::Colon) {
          r.name = advance().text;
          advance();
        }
        r.type_expr = parse_type();
        p->returns.push_back(r);
        if (!accept(Tok::Comma)) break;
      }
      expect(Tok::RParen);
      return;
    }
    // Unparenthesized list; names are allowed here too: -> found: bool, index: int
    for (;;) {
      ReturnSpec r;
      r.loc = cur().loc;
      if (at(Tok::Ident) && peek().kind == Tok::Colon) {
        r.name = advance().text;
        advance();
      }
      r.type_expr = parse_type();
      p->returns.push_back(r);
      if (!accept(Tok::Comma)) break;
    }
  }

  StructDef* parse_struct() {
    auto* s = ast_.make<StructDef>(cur().loc);
    s->is_union = at(Tok::KwUnion);
    advance();
    if (accept(Tok::LParen)) {  // struct ($T: Type, $N: int): a polymorphic struct
      while (!at(Tok::RParen)) {
        Decl* d = parse_param(nullptr);
        if (d->is_using) fail(d->loc, "'using' is not allowed on a struct parameter");
        d->is_baked = true;  // struct parameters are always compile-time values
        s->params.push_back(d);
        if (!accept(Tok::Comma)) break;
      }
      expect(Tok::RParen, "after struct parameters");
      if (s->params.empty()) fail(s->loc, "a parameterized struct needs at least one parameter");
    }
    auto reject_align = [&] {
      if (at_directive("align"))
        fail(cur().loc, "#align cannot be applied to a struct (in Jai it has no effect there); "
             "put #align on its members");
    };
    while (at(Tok::Directive)) {
      if (cur().text == "modify") fail(cur().loc, "#modify on structs is not supported yet");
      reject_align();
      // #type_info_none etc. are accepted and ignored
      advance();
    }
    expect(Tok::LBrace, "to begin struct body");
    while (!at(Tok::RBrace)) {
      if (at(Tok::Eof)) fail(s->loc, "unterminated struct body (missing '}')");
      if (accept(Tok::Semi)) continue;
      s->members.push_back(parse_struct_member());
    }
    advance();
    reject_align();
    return s;
  }

 public:
  // One member declaration of a struct body (or `#insert expr;`).
  Decl* parse_struct_member() {
    if (at_directive("as")) fail(cur().loc, "#as in structs is not supported yet");
    if (at_directive("align")) fail(cur().loc, "#align must follow a member's type: name: T #align N;");
    SourceLoc loc = cur().loc;
    auto* d = ast_.make<Decl>(loc);
    if (at_directive("place")) {  // `#place m;`: the next members start at m's offset
      advance();
      d->place_name = expect_ident("member name after #place");
      expect(Tok::Semi, "after #place");
      return d;
    }
    if (at_directive("insert")) {  // members made at compile time
      advance();
      reject_insert_modifiers();
      d->insert = parse_expr();
      expect(Tok::Semi, "after #insert");
      return d;
    }
    if (accept(Tok::KwUsing)) d->is_using = true;
    if (at(Tok::KwStruct) || at(Tok::KwUnion)) {  // anonymous member: `union { a: u32; b: f32; }`
      if (d->is_using) fail(loc, "expected a member name after 'using'");
      d->names.push_back({"", loc});
      d->type_expr = parse_struct();
      return d;
    }
    for (;;) {
      d->names.push_back({expect_ident("member name"), cur().loc});
      if (!accept(Tok::Comma)) break;
    }
    parse_decl_rest(d, /*member=*/true);
    if (d->is_const && d->is_using) fail(loc, "'using' is not allowed on a struct constant");
    return d;
  }

  // The whole file as statements (an inserted string).
  std::vector<Stmt*> parse_statements(bool top_level) {
    std::vector<Stmt*> out;
    while (!at(Tok::Eof)) {
      if (accept(Tok::Semi)) continue;
      out.push_back(parse_stmt(top_level));
    }
    return out;
  }

  // The whole file as one expression (an inserted string).
  Expr* parse_whole_expr() {
    Expr* e = parse_expr();
    accept(Tok::Semi);
    if (!at(Tok::Eof)) fail(cur().loc, "expected the end of the inserted expression, found ", describe(cur()));
    return e;
  }

  // The whole file as struct members (an inserted string).
  std::vector<Decl*> parse_struct_members() {
    std::vector<Decl*> out;
    while (!at(Tok::Eof)) {
      if (accept(Tok::Semi)) continue;
      out.push_back(parse_struct_member());
    }
    return out;
  }

 private:

  EnumDef* parse_enum() {
    auto* e = ast_.make<EnumDef>(cur().loc);
    e->is_flags = at(Tok::KwEnumFlags);
    advance();
    if (!at(Tok::LBrace)) e->base_type = parse_type();
    while (at(Tok::Directive)) advance();  // #specified etc.
    expect(Tok::LBrace, "to begin enum body");
    while (!at(Tok::RBrace)) {
      if (accept(Tok::Semi)) continue;
      EnumMemberDef m;
      m.loc = cur().loc;
      m.name = expect_ident("enum member name");
      if (accept(Tok::ColonColon)) m.value = parse_expr();
      expect(Tok::Semi, "after enum member");
      e->members.push_back(m);
    }
    advance();
    return e;
  }
};

}  // namespace

std::vector<Stmt*> parse_file(AstContext& ast, const SourceManager& sm, uint32_t file_id) {
  Parser p(ast, sm, file_id);
  return p.parse_top_level();
}

std::vector<Stmt*> parse_source_stmts(AstContext& ast, const SourceManager& sm, uint32_t file_id, bool top_level) {
  Parser p(ast, sm, file_id);
  return top_level ? p.parse_top_level() : p.parse_statements(false);
}

Expr* parse_source_expr(AstContext& ast, const SourceManager& sm, uint32_t file_id) {
  Parser p(ast, sm, file_id);
  return p.parse_whole_expr();
}

std::vector<Decl*> parse_source_members(AstContext& ast, const SourceManager& sm, uint32_t file_id) {
  Parser p(ast, sm, file_id);
  return p.parse_struct_members();
}

}  // namespace ojay
