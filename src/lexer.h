#pragma once

#include <string>
#include <vector>

#include "support.h"

namespace ojay {

enum class Tok {
  Eof,
  Ident,
  Int,
  Float,
  String,
  Directive,  // #name   (text = name without '#')

  // keywords
  KwIf, KwElse, KwThen, KwIfx, KwCase, KwWhile, KwFor, KwBreak, KwContinue,
  KwReturn, KwDefer, KwStruct, KwUnion, KwEnum, KwEnumFlags, KwUsing, KwCast,
  KwXx, KwTrue, KwFalse, KwNull, KwInline, KwRemove,

  // punctuation
  LParen, RParen, LBrace, RBrace, LBracket, RBracket,
  Comma, Semi, Colon, ColonColon, ColonEq,
  Dot, DotDot, DotLBrace, DotLBracket, DotStar,
  Arrow, FatArrow, Dollar, Backtick, TripleDash,
  Eq, EqEq, NotEq, Lt, LtEq, Gt, GtEq,
  Plus, Minus, Star, Slash, Percent, Amp, Pipe, Caret, Tilde, Bang,
  AndAnd, OrOr, Shl, Shr,
  PlusEq, MinusEq, StarEq, SlashEq, PercentEq, AmpEq, PipeEq, CaretEq, ShlEq, ShrEq,
};

const char* tok_name(Tok t);

struct Token {
  Tok kind = Tok::Eof;
  SourceLoc loc;
  std::string text;      // identifier / directive name / decoded string
  unsigned __int128 ival = 0;
  double fval = 0;
};

// Tokenizes a whole file. Throws CompileError on malformed input.
std::vector<Token> lex(const SourceManager& sm, uint32_t file_id);

}  // namespace ojay
