#pragma once

#include <vector>

#include "ast.h"

namespace ojay {

// Parses one source file into a list of top-level statements.
std::vector<Stmt*> parse_file(AstContext& ast, const SourceManager& sm, uint32_t file_id);

// Text made at compile time for #insert (a file registered with the
// SourceManager): statements (top-level declarations with `top_level`), one
// expression, or struct members.
std::vector<Stmt*> parse_source_stmts(AstContext& ast, const SourceManager& sm, uint32_t file_id, bool top_level);
Expr* parse_source_expr(AstContext& ast, const SourceManager& sm, uint32_t file_id);
std::vector<Decl*> parse_source_members(AstContext& ast, const SourceManager& sm, uint32_t file_id);

}  // namespace ojay
