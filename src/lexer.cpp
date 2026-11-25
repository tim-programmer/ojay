#include "lexer.h"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace ojay {

const char* tok_name(Tok t) {
  switch (t) {
    case Tok::Eof: return "end of file";
    case Tok::Ident: return "identifier";
    case Tok::Int: return "integer literal";
    case Tok::Float: return "float literal";
    case Tok::String: return "string literal";
    case Tok::Directive: return "directive";
    case Tok::KwIf: return "'if'";
    case Tok::KwElse: return "'else'";
    case Tok::KwThen: return "'then'";
    case Tok::KwIfx: return "'ifx'";
    case Tok::KwCase: return "'case'";
    case Tok::KwWhile: return "'while'";
    case Tok::KwFor: return "'for'";
    case Tok::KwBreak: return "'break'";
    case Tok::KwContinue: return "'continue'";
    case Tok::KwReturn: return "'return'";
    case Tok::KwDefer: return "'defer'";
    case Tok::KwStruct: return "'struct'";
    case Tok::KwUnion: return "'union'";
    case Tok::KwEnum: return "'enum'";
    case Tok::KwEnumFlags: return "'enum_flags'";
    case Tok::KwUsing: return "'using'";
    case Tok::KwCast: return "'cast'";
    case Tok::KwXx: return "'xx'";
    case Tok::KwTrue: return "'true'";
    case Tok::KwFalse: return "'false'";
    case Tok::KwNull: return "'null'";
    case Tok::KwInline: return "'inline'";
    case Tok::KwRemove: return "'remove'";
    case Tok::LParen: return "'('";
    case Tok::RParen: return "')'";
    case Tok::LBrace: return "'{'";
    case Tok::RBrace: return "'}'";
    case Tok::LBracket: return "'['";
    case Tok::RBracket: return "']'";
    case Tok::Comma: return "','";
    case Tok::Semi: return "';'";
    case Tok::Colon: return "':'";
    case Tok::ColonColon: return "'::'";
    case Tok::ColonEq: return "':='";
    case Tok::Dot: return "'.'";
    case Tok::DotDot: return "'..'";
    case Tok::DotLBrace: return "'.{'";
    case Tok::DotLBracket: return "'.['";
    case Tok::DotStar: return "'.*'";
    case Tok::Arrow: return "'->'";
    case Tok::Dollar: return "'$'";
    case Tok::TripleDash: return "'---'";
    case Tok::Eq: return "'='";
    case Tok::EqEq: return "'=='";
    case Tok::NotEq: return "'!='";
    case Tok::Lt: return "'<'";
    case Tok::LtEq: return "'<='";
    case Tok::Gt: return "'>'";
    case Tok::GtEq: return "'>='";
    case Tok::Plus: return "'+'";
    case Tok::Minus: return "'-'";
    case Tok::Star: return "'*'";
    case Tok::Slash: return "'/'";
    case Tok::Percent: return "'%'";
    case Tok::Amp: return "'&'";
    case Tok::Pipe: return "'|'";
    case Tok::Caret: return "'^'";
    case Tok::Tilde: return "'~'";
    case Tok::Bang: return "'!'";
    case Tok::AndAnd: return "'&&'";
    case Tok::OrOr: return "'||'";
    case Tok::Shl: return "'<<'";
    case Tok::Shr: return "'>>'";
    case Tok::PlusEq: return "'+='";
    case Tok::MinusEq: return "'-='";
    case Tok::StarEq: return "'*='";
    case Tok::SlashEq: return "'/='";
    case Tok::PercentEq: return "'%='";
    case Tok::AmpEq: return "'&='";
    case Tok::PipeEq: return "'|='";
    case Tok::CaretEq: return "'^='";
    case Tok::ShlEq: return "'<<='";
    case Tok::ShrEq: return "'>>='";
  }
  return "?";
}

namespace {

const std::unordered_map<std::string, Tok>& keywords() {
  static const std::unordered_map<std::string, Tok> kw = {
      {"if", Tok::KwIf},         {"else", Tok::KwElse},
      {"then", Tok::KwThen},     {"ifx", Tok::KwIfx},
      {"case", Tok::KwCase},     {"while", Tok::KwWhile},
      {"for", Tok::KwFor},       {"break", Tok::KwBreak},
      {"continue", Tok::KwContinue}, {"return", Tok::KwReturn},
      {"defer", Tok::KwDefer},   {"struct", Tok::KwStruct},
      {"union", Tok::KwUnion},   {"enum", Tok::KwEnum},
      {"enum_flags", Tok::KwEnumFlags}, {"using", Tok::KwUsing},
      {"cast", Tok::KwCast},     {"xx", Tok::KwXx},
      {"true", Tok::KwTrue},     {"false", Tok::KwFalse},
      {"null", Tok::KwNull},     {"inline", Tok::KwInline},
      {"remove", Tok::KwRemove},
  };
  return kw;
}

class Lexer {
 public:
  Lexer(const SourceManager& sm, uint32_t file) : file_(file), src_(sm.get(file).text) {}

  std::vector<Token> run() {
    std::vector<Token> out;
    for (;;) {
      skip_space_and_comments();
      Token t;
      t.loc = here();
      if (pos_ >= src_.size()) {
        t.kind = Tok::Eof;
        out.push_back(t);
        return out;
      }
      lex_one(t);
      // `#string TERM ... TERM` here-strings are handled right after the directive.
      if (t.kind == Tok::Directive && t.text == "string") {
        out.push_back(t);
        Token s;
        s.loc = here();
        s.kind = Tok::String;
        s.text = lex_here_string();
        out.push_back(s);
        continue;
      }
      out.push_back(std::move(t));
    }
  }

 private:
  uint32_t file_;
  const std::string& src_;
  size_t pos_ = 0;
  uint32_t line_ = 1, col_ = 1;

  SourceLoc here() const { return SourceLoc{file_, line_, col_}; }
  char peek(size_t o = 0) const { return pos_ + o < src_.size() ? src_[pos_ + o] : '\0'; }
  char get() {
    char c = src_[pos_++];
    if (c == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    return c;
  }

  void skip_space_and_comments() {
    for (;;) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        get();
      } else if (c == '/' && peek(1) == '/') {
        while (pos_ < src_.size() && peek() != '\n') get();
      } else if (c == '/' && peek(1) == '*') {
        // Block comments nest in Jai.
        SourceLoc start = here();
        get();
        get();
        int depth = 1;
        while (depth > 0) {
          if (pos_ >= src_.size()) fail(start, "unterminated block comment");
          if (peek() == '/' && peek(1) == '*') {
            get(); get(); ++depth;
          } else if (peek() == '*' && peek(1) == '/') {
            get(); get(); --depth;
          } else {
            get();
          }
        }
      } else {
        return;
      }
    }
  }

  static bool ident_start(char c) { return std::isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }
  static bool ident_char(char c) { return ident_start(c) || std::isdigit((unsigned char)c); }

  void lex_one(Token& t) {
    char c = peek();
    if (ident_start(c)) {
      std::string s;
      while (ident_char(peek())) s += get();
      auto it = keywords().find(s);
      if (it != keywords().end()) {
        t.kind = it->second;
      } else {
        t.kind = Tok::Ident;
      }
      t.text = std::move(s);
      return;
    }
    if (std::isdigit((unsigned char)c)) return lex_number(t);
    if (c == '"') return lex_string(t);
    if (c == '#') {
      get();
      if (!ident_start(peek())) fail(t.loc, "expected directive name after '#'");
      std::string s;
      while (ident_char(peek())) s += get();
      t.kind = Tok::Directive;
      t.text = std::move(s);
      return;
    }
    if (c == '@') {
      // Notes (@foo) are accepted and ignored.
      get();
      while (ident_char(peek())) get();
      skip_space_and_comments();
      Token next;
      next.loc = here();
      if (pos_ >= src_.size()) {
        t.kind = Tok::Eof;
        return;
      }
      lex_one(next);
      t = std::move(next);
      return;
    }
    lex_punct(t);
  }

  void lex_number(Token& t) {
    unsigned __int128 v = 0;
    auto overflow = [&]() { fail(t.loc, "integer literal is too large"); };
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
      get(); get();
      bool any = false;
      while (std::isxdigit((unsigned char)peek()) || peek() == '_') {
        char ch = get();
        if (ch == '_') continue;
        int d = std::isdigit((unsigned char)ch) ? ch - '0' : (std::tolower(ch) - 'a' + 10);
        v = v * 16 + d;
        if (v > (unsigned __int128)UINT64_MAX) overflow();
        any = true;
      }
      if (!any) fail(t.loc, "malformed hex literal");
      t.kind = Tok::Int;
      t.ival = v;
      return;
    }
    if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
      get(); get();
      bool any = false;
      while (peek() == '0' || peek() == '1' || peek() == '_') {
        char ch = get();
        if (ch == '_') continue;
        v = v * 2 + (ch - '0');
        if (v > (unsigned __int128)UINT64_MAX) overflow();
        any = true;
      }
      if (!any) fail(t.loc, "malformed binary literal");
      t.kind = Tok::Int;
      t.ival = v;
      return;
    }
    std::string digits;
    bool is_float = false;
    while (std::isdigit((unsigned char)peek()) || peek() == '_') {
      char ch = get();
      if (ch != '_') digits += ch;
    }
    // A '.' followed by a digit makes a float; '..' is a range operator.
    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
      is_float = true;
      digits += get();
      while (std::isdigit((unsigned char)peek()) || peek() == '_') {
        char ch = get();
        if (ch != '_') digits += ch;
      }
    }
    if ((peek() == 'e' || peek() == 'E') &&
        (std::isdigit((unsigned char)peek(1)) ||
         ((peek(1) == '-' || peek(1) == '+') && std::isdigit((unsigned char)peek(2))))) {
      is_float = true;
      digits += get();
      if (peek() == '-' || peek() == '+') digits += get();
      while (std::isdigit((unsigned char)peek())) digits += get();
    }
    if (is_float) {
      t.kind = Tok::Float;
      t.fval = std::strtod(digits.c_str(), nullptr);
      return;
    }
    for (char ch : digits) {
      v = v * 10 + (ch - '0');
      if (v > (unsigned __int128)UINT64_MAX) overflow();
    }
    t.kind = Tok::Int;
    t.ival = v;
  }

  void lex_string(Token& t) {
    get();  // opening quote
    std::string s;
    for (;;) {
      if (pos_ >= src_.size() || peek() == '\n') fail(t.loc, "unterminated string literal");
      char c = get();
      if (c == '"') break;
      if (c != '\\') {
        s += c;
        continue;
      }
      SourceLoc esc = here();
      char e = get();
      switch (e) {
        case 'n': s += '\n'; break;
        case 't': s += '\t'; break;
        case 'r': s += '\r'; break;
        case '0': s += '\0'; break;
        case 'e': s += '\x1b'; break;
        case '\\': s += '\\'; break;
        case '"': s += '"'; break;
        case '\'': s += '\''; break;
        case 'x': {
          int val = 0;
          for (int i = 0; i < 2; ++i) {
            char h = peek();
            if (!std::isxdigit((unsigned char)h)) fail(esc, "expected two hex digits after \\x");
            get();
            val = val * 16 + (std::isdigit((unsigned char)h) ? h - '0' : std::tolower(h) - 'a' + 10);
          }
          s += static_cast<char>(val);
          break;
        }
        case 'd': {
          int val = 0;
          for (int i = 0; i < 3; ++i) {
            char h = peek();
            if (!std::isdigit((unsigned char)h)) fail(esc, "expected three decimal digits after \\d");
            get();
            val = val * 10 + (h - '0');
          }
          if (val > 255) fail(esc, "\\d escape out of range");
          s += static_cast<char>(val);
          break;
        }
        default:
          fail(esc, "unknown escape sequence '\\", e, "'");
      }
    }
    t.kind = Tok::String;
    t.text = std::move(s);
  }

  // #string TERM
  // ...text...
  // TERM
  std::string lex_here_string() {
    while (peek() == ' ' || peek() == '\t') get();
    SourceLoc loc = here();
    std::string term;
    while (ident_char(peek())) term += get();
    if (term.empty()) fail(loc, "expected terminator identifier after #string");
    // Skip to end of line.
    while (pos_ < src_.size() && peek() != '\n') get();
    if (pos_ < src_.size()) get();
    std::string body;
    for (;;) {
      if (pos_ >= src_.size()) fail(loc, "unterminated #string (missing '", term, "')");
      // Check for terminator at the start of a line (after optional whitespace).
      size_t save_pos = pos_;
      uint32_t save_line = line_, save_col = col_;
      while (peek() == ' ' || peek() == '\t') get();
      if (src_.compare(pos_, term.size(), term) == 0 && !ident_char(peek(term.size()))) {
        for (size_t i = 0; i < term.size(); ++i) get();
        if (!body.empty() && body.back() == '\n') body.pop_back();
        return body;
      }
      pos_ = save_pos;
      line_ = save_line;
      col_ = save_col;
      while (pos_ < src_.size() && peek() != '\n') body += get();
      if (pos_ < src_.size()) body += get();
    }
  }

  void lex_punct(Token& t) {
    char c = get();
    auto match = [&](char ch) {
      if (peek() == ch) {
        get();
        return true;
      }
      return false;
    };
    switch (c) {
      case '(': t.kind = Tok::LParen; return;
      case ')': t.kind = Tok::RParen; return;
      case '{': t.kind = Tok::LBrace; return;
      case '}': t.kind = Tok::RBrace; return;
      case '[': t.kind = Tok::LBracket; return;
      case ']': t.kind = Tok::RBracket; return;
      case ',': t.kind = Tok::Comma; return;
      case ';': t.kind = Tok::Semi; return;
      case '$': t.kind = Tok::Dollar; return;
      case '~': t.kind = Tok::Tilde; return;
      case ':':
        if (match(':')) t.kind = Tok::ColonColon;
        else if (match('=')) t.kind = Tok::ColonEq;
        else t.kind = Tok::Colon;
        return;
      case '.':
        if (match('.')) t.kind = Tok::DotDot;
        else if (match('{')) t.kind = Tok::DotLBrace;
        else if (match('[')) t.kind = Tok::DotLBracket;
        else if (match('*')) t.kind = Tok::DotStar;
        else t.kind = Tok::Dot;
        return;
      case '-':
        if (peek() == '-' && peek(1) == '-') {
          get(); get();
          t.kind = Tok::TripleDash;
        } else if (match('>')) t.kind = Tok::Arrow;
        else if (match('=')) t.kind = Tok::MinusEq;
        else t.kind = Tok::Minus;
        return;
      case '=': t.kind = match('=') ? Tok::EqEq : Tok::Eq; return;
      case '!': t.kind = match('=') ? Tok::NotEq : Tok::Bang; return;
      case '<':
        if (match('<')) t.kind = match('=') ? Tok::ShlEq : Tok::Shl;
        else t.kind = match('=') ? Tok::LtEq : Tok::Lt;
        return;
      case '>':
        if (match('>')) t.kind = match('=') ? Tok::ShrEq : Tok::Shr;
        else t.kind = match('=') ? Tok::GtEq : Tok::Gt;
        return;
      case '+': t.kind = match('=') ? Tok::PlusEq : Tok::Plus; return;
      case '*': t.kind = match('=') ? Tok::StarEq : Tok::Star; return;
      case '/': t.kind = match('=') ? Tok::SlashEq : Tok::Slash; return;
      case '%': t.kind = match('=') ? Tok::PercentEq : Tok::Percent; return;
      case '^': t.kind = match('=') ? Tok::CaretEq : Tok::Caret; return;
      case '&':
        if (match('&')) t.kind = Tok::AndAnd;
        else t.kind = match('=') ? Tok::AmpEq : Tok::Amp;
        return;
      case '|':
        if (match('|')) t.kind = Tok::OrOr;
        else t.kind = match('=') ? Tok::PipeEq : Tok::Pipe;
        return;
      default:
        fail(t.loc, "unexpected character '", std::string(1, c), "'");
    }
  }
};

}  // namespace

std::vector<Token> lex(const SourceManager& sm, uint32_t file_id) {
  Lexer lx(sm, file_id);
  return lx.run();
}

}  // namespace ojay
