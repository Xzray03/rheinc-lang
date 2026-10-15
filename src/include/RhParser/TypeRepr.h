#pragma once

#include "RhParser/AstNode.h"
#include "RhParser/Expr.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rhc {

struct TypeReprNode : AstNode {
    using AstNode::AstNode;
};

struct BuiltinTypeRepr final : TypeReprNode {
    TokenKind typeKind;

    BuiltinTypeRepr(TokenKind kind, SourceRange range)
        : TypeReprNode(AstKind::BuiltinTypeRepr, range), typeKind(kind) {}
};

struct NamedTypeRepr final : TypeReprNode {
    std::string name;

    NamedTypeRepr(std::string typeName, SourceRange range)
        : TypeReprNode(AstKind::NamedTypeRepr, range), name(std::move(typeName)) {}
};

struct TypeArgumentRepr final : AstNode {
    std::string name;
    TypeReprNode* type = nullptr;
    ExprNode* value = nullptr;

    TypeArgumentRepr(std::string argumentName, TypeReprNode* argumentType,
                     ExprNode* argumentValue, SourceRange range)
        : AstNode(AstKind::TypeArgumentRepr, range), name(std::move(argumentName)),
          type(argumentType), value(argumentValue) {}
};

struct CallTypeRepr final : TypeReprNode {
    std::string name;
    std::vector<TypeArgumentRepr*> arguments;
    bool angleBracket = false;

    CallTypeRepr(std::string typeName, std::vector<TypeArgumentRepr*> args,
                 bool isAngleBracket, SourceRange range)
        : TypeReprNode(AstKind::CallTypeRepr, range), name(std::move(typeName)),
          arguments(std::move(args)), angleBracket(isAngleBracket) {}
};

struct ArrayTypeRepr final : TypeReprNode {
    std::vector<ExprNode*> dimensions;
    TypeReprNode* elementType = nullptr;

    ArrayTypeRepr(std::vector<ExprNode*> sizes, TypeReprNode* element, SourceRange range)
        : TypeReprNode(AstKind::ArrayTypeRepr, range), dimensions(std::move(sizes)),
          elementType(element) {}
};

struct PointerTypeRepr final : TypeReprNode {
    TypeReprNode* pointee = nullptr;

    explicit PointerTypeRepr(TypeReprNode* targetType, SourceRange range)
        : TypeReprNode(AstKind::PointerTypeRepr, range), pointee(targetType) {}
};

struct FunctionPointerTypeRepr final : TypeReprNode {
    std::vector<std::pair<std::string, TypeReprNode*>> parameters;
    TypeReprNode* returnType = nullptr;
    bool bare = false;

    FunctionPointerTypeRepr(std::vector<std::pair<std::string, TypeReprNode*>> params,
                            TypeReprNode* resultType, bool isBare, SourceRange range)
        : TypeReprNode(AstKind::FunctionPointerTypeRepr, range),
          parameters(std::move(params)), returnType(resultType), bare(isBare) {}
};

struct ErrorTypeRepr final : TypeReprNode {
    ErrorTypeRepr(SourceRange range) : TypeReprNode(AstKind::ErrorTypeRepr, range) {}
};

} // namespace rhc
