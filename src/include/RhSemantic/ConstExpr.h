#pragma once

#include "RhParser/AstNode.h"
#include "RhParser/Expr.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace rhc {

// Named compile-time constants visible to the evaluator (e.g. earlier cases
// of the enum currently being evaluated).
using ConstTable = std::unordered_map<std::string, std::int64_t>;

// Parses an integer literal: decimal, 0x hex, 0b binary (underscores allowed
// as digit separators).
std::optional<std::int64_t> parseIntegerLiteral(const std::string& text);

// Evaluates an integer constant expression: integer/char literals, enum
// constants from `constants`, unary + - ~, and binary arithmetic/bitwise ops.
// Returns std::nullopt for anything non-constant.
std::optional<std::int64_t> evaluateConstInt(ExprNode* expr,
                                             const ConstTable& constants = {});

} // namespace rhc
