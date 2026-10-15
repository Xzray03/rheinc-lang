#pragma once

#include "RhParser/AstNode.h"
#include "RhParser/Expr.h"

#include <string>
#include <vector>

namespace rhc {

struct TypeReprNode;

struct PatternNode : AstNode {
    using AstNode::AstNode;
};

struct ExpressionPattern final : PatternNode {
    ExprNode* expression = nullptr;

    explicit ExpressionPattern(ExprNode* expr, SourceRange range)
        : PatternNode(AstKind::ExpressionPattern, range), expression(expr) {}
};

struct PatternBinding final : AstNode {
    std::string name;
    TypeReprNode* type = nullptr;
    PatternNode* nestedPattern = nullptr;

    PatternBinding(std::string bindingName, TypeReprNode* bindingType,
                   PatternNode* nested, SourceRange range)
        : AstNode(AstKind::PatternBinding, range), name(std::move(bindingName)),
          type(bindingType), nestedPattern(nested) {}
};

struct ConstructorPattern final : PatternNode {
    std::string constructorName;
    std::vector<PatternBinding*> bindings;

    ConstructorPattern(std::string name, std::vector<PatternBinding*> patternBindings,
                       SourceRange range)
        : PatternNode(AstKind::ConstructorPattern, range),
          constructorName(std::move(name)), bindings(std::move(patternBindings)) {}
};

} // namespace rhc
