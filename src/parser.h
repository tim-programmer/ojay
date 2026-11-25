#pragma once

#include <vector>

#include "ast.h"

namespace ojay {

// Parses one source file into a list of top-level statements.
std::vector<Stmt*> parse_file(AstContext& ast, const SourceManager& sm, uint32_t file_id);

}  // namespace ojay
