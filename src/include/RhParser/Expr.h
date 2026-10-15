#pragma once

#include "RhParser/AstNode.h"

#include <string>
#include <utility>
#include <vector>

namespace rhc {

struct TypeReprNode;
struct CompoundStmt;

struct ExprNode : AstNode {
    using AstNode::AstNode;
};

struct IdentifierExpr final : ExprNode {
    std::string name;

    IdentifierExpr(std::string identifier, SourceRange range)
        : ExprNode(AstKind::IdentifierExpr, range), name(std::move(identifier)) {}
};

struct IntegerLiteralExpr final : ExprNode {
    std::string text;

    IntegerLiteralExpr(std::string literalText, SourceRange range)
        : ExprNode(AstKind::IntegerLiteralExpr, range), text(std::move(literalText)) {}
};

struct CharLiteralExpr final : ExprNode {
    std::string text;

    CharLiteralExpr(std::string literalText, SourceRange range)
        : ExprNode(AstKind::CharLiteralExpr, range), text(std::move(literalText)) {}
};

struct BoolLiteralExpr final : ExprNode {
    bool value = false;

    explicit BoolLiteralExpr(bool literalValue, SourceRange range)
        : ExprNode(AstKind::BoolLiteralExpr, range), value(literalValue) {}
};

struct NullLiteralExpr final : ExprNode {
    explicit NullLiteralExpr(SourceRange range) : ExprNode(AstKind::NullLiteralExpr, range) {}
};

struct StringLiteralExpr final : ExprNode {
    std::string text;
    bool raw = false;

    StringLiteralExpr(std::string literalText, bool isRaw, SourceRange range)
        : ExprNode(AstKind::StringLiteralExpr, range), text(std::move(literalText)), raw(isRaw) {}
};

struct ArrayLiteralExpr final : ExprNode {
    std::vector<ExprNode*> elements;

    explicit ArrayLiteralExpr(std::vector<ExprNode*> values, SourceRange range)
        : ExprNode(AstKind::ArrayLiteralExpr, range), elements(std::move(values)) {}
};

struct CallExpr final : ExprNode {
    ExprNode* callee = nullptr;
    std::vector<ExprNode*> arguments;

    CallExpr(ExprNode* callCallee, std::vector<ExprNode*> args, SourceRange range)
        : ExprNode(AstKind::CallExpr, range), callee(callCallee),
          arguments(std::move(args)) {}
};

struct BinaryExpr final : ExprNode {
    ExprNode* lhs = nullptr;
    TokenKind op = TokenKind::Unknown;
    ExprNode* rhs = nullptr;

    BinaryExpr(ExprNode* left, TokenKind operation, ExprNode* right, SourceRange range)
        : ExprNode(AstKind::BinaryExpr, range), lhs(left), op(operation), rhs(right) {}
};

struct UnaryExpr final : ExprNode {
    TokenKind op = TokenKind::Unknown;
    ExprNode* operand = nullptr;
    bool postfix = false;

    UnaryExpr(TokenKind operation, ExprNode* value, bool isPostfix, SourceRange range)
        : ExprNode(AstKind::UnaryExpr, range), op(operation), operand(value), postfix(isPostfix) {}
};

struct TernaryExpr final : ExprNode {
    ExprNode* condition = nullptr;
    ExprNode* trueValue = nullptr;
    ExprNode* falseValue = nullptr;

    TernaryExpr(ExprNode* cond, ExprNode* whenTrue, ExprNode* whenFalse, SourceRange range)
        : ExprNode(AstKind::TernaryExpr, range), condition(cond), trueValue(whenTrue),
          falseValue(whenFalse) {}
};

struct MemberExpr final : ExprNode {
    ExprNode* object = nullptr;
    std::string memberName;
    bool isArrow = false;

    MemberExpr(ExprNode* base, std::string name, bool arrow, SourceRange range)
        : ExprNode(AstKind::MemberExpr, range), object(base), memberName(std::move(name)),
          isArrow(arrow) {}
};

struct ArrayIndexExpr final : ExprNode {
    ExprNode* object = nullptr;
    ExprNode* index = nullptr;

    ArrayIndexExpr(ExprNode* base, ExprNode* indexExpr, SourceRange range)
        : ExprNode(AstKind::ArrayIndexExpr, range), object(base), index(indexExpr) {}
};

struct NamedArgumentExpr final : ExprNode {
    std::string name;
    ExprNode* value = nullptr;

    NamedArgumentExpr(std::string argumentName, ExprNode* argumentValue, SourceRange range)
        : ExprNode(AstKind::NamedArgumentExpr, range), name(std::move(argumentName)),
          value(argumentValue) {}
};

struct LambdaExpr final : ExprNode {
    std::vector<std::string> typeParameters;
    std::vector<std::pair<std::string, TypeReprNode*>> parameters;
    TypeReprNode* returnType = nullptr;
    CompoundStmt* body = nullptr;

    LambdaExpr(std::vector<std::string> genericParams,
               std::vector<std::pair<std::string, TypeReprNode*>> params,
               TypeReprNode* resultType, CompoundStmt* lambdaBody, SourceRange range)
        : ExprNode(AstKind::LambdaExpr, range), typeParameters(std::move(genericParams)),
          parameters(std::move(params)), returnType(resultType), body(lambdaBody) {}
};

struct SliceExpr final : ExprNode {
    ExprNode* start = nullptr;
    ExprNode* end = nullptr;
    ExprNode* step = nullptr;

    SliceExpr(ExprNode* startValue, ExprNode* endValue, ExprNode* stepValue,
              SourceRange range)
        : ExprNode(AstKind::SliceExpr, range), start(startValue), end(endValue),
          step(stepValue) {}
};

struct TypeExpr final : ExprNode {
    TypeReprNode* type = nullptr;

    explicit TypeExpr(TypeReprNode* typeRepr, SourceRange range)
        : ExprNode(AstKind::TypeExpr, range), type(typeRepr) {}
};

struct ErrorExpr final : ExprNode {
    ErrorExpr(SourceRange range) : ExprNode(AstKind::ErrorExpr, range) {}
};

} // namespace rhc
