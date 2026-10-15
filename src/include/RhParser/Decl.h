#pragma once

#include "RhParser/Stmt.h"
#include "RhParser/TypeRepr.h"

#include <string>
#include <utility>
#include <vector>

namespace rhc {

enum class FunctionModifier {
    Async,
    Coroutine,
    Generator,
};

struct DeclNode : AstNode {
    using AstNode::AstNode;
};

struct ParamDecl final : DeclNode {
    std::string name;
    TypeReprNode* type = nullptr;

    ParamDecl(std::string paramName, TypeReprNode* paramType, SourceRange range)
        : DeclNode(AstKind::ParamDecl, range), name(std::move(paramName)), type(paramType) {}
};

struct FunctionDecl final : DeclNode {
    std::string name;
    std::vector<ParamDecl*> parameters;
    TypeReprNode* returnType = nullptr;
    CompoundStmt* body = nullptr;
    bool isProcedure = false;
    std::vector<FunctionModifier> modifiers;

    FunctionDecl(std::string declName, bool procedure, SourceRange range)
        : DeclNode(AstKind::FunctionDecl, range), name(std::move(declName)),
          isProcedure(procedure) {}
};

struct StructField final : DeclNode {
    std::string name;
    TypeReprNode* type = nullptr;
    ExprNode* bitWidth = nullptr;

    StructField(std::string fieldName, TypeReprNode* fieldType, ExprNode* width,
                SourceRange range)
        : DeclNode(AstKind::StructField, range), name(std::move(fieldName)),
          type(fieldType), bitWidth(width) {}
};

struct StructDecl final : DeclNode {
    std::string name;
    std::vector<StructField*> fields;

    StructDecl(std::string structName, std::vector<StructField*> structFields,
               SourceRange range)
        : DeclNode(AstKind::StructDecl, range), name(std::move(structName)),
          fields(std::move(structFields)) {}
};

struct GlobalVarDecl final : DeclNode {
    std::string name;
    TypeReprNode* type = nullptr;
    ExprNode* initializer = nullptr;
    bool isConst = false;
    StorageModifier storage = StorageModifier::None;

    GlobalVarDecl(std::string variableName, TypeReprNode* variableType,
                  ExprNode* init, bool constant, StorageModifier storageModifier,
                  SourceRange range)
        : DeclNode(AstKind::GlobalVarDecl, range), name(std::move(variableName)),
          type(variableType), initializer(init), isConst(constant), storage(storageModifier) {}
};

struct GenericDecl final : DeclNode {
    std::vector<std::string> typeParameters;
    DeclNode* declaration = nullptr;

    GenericDecl(std::vector<std::string> params, DeclNode* nested,
                SourceRange range)
        : DeclNode(AstKind::GenericDecl, range), typeParameters(std::move(params)),
          declaration(nested) {}
};

struct EnumField final : DeclNode {
    std::string name;
    std::vector<std::pair<std::string, TypeReprNode*>> fields;
    ExprNode* value = nullptr;

    EnumField(std::string fieldName,
              std::vector<std::pair<std::string, TypeReprNode*>> fieldTypes,
              ExprNode* aliasValue, SourceRange range)
        : DeclNode(AstKind::EnumField, range), name(std::move(fieldName)),
          fields(std::move(fieldTypes)), value(aliasValue) {}
};

struct EnumDecl final : DeclNode {
    std::string name;
    std::vector<EnumField*> fields;

    EnumDecl(std::string enumName, std::vector<EnumField*> enumFields,
             SourceRange range)
        : DeclNode(AstKind::EnumDecl, range), name(std::move(enumName)),
          fields(std::move(enumFields)) {}
};

struct TestAnnotationDecl;

struct TranslationUnitDecl final : DeclNode {
    std::vector<DeclNode*> declarations;
    std::vector<TestAnnotationDecl*> testAnnotations;

    TranslationUnitDecl() : DeclNode(AstKind::TranslationUnit, {}) {}
};

struct TestAnnotationDecl final : DeclNode {
    std::string name;
    std::vector<ExprNode*> arguments;

    TestAnnotationDecl(std::string testName, std::vector<ExprNode*> args,
                       SourceRange range)
        : DeclNode(AstKind::TestAnnotationDecl, range), name(std::move(testName)),
          arguments(std::move(args)) {}
};

struct ErrorDecl final : DeclNode {
    ErrorDecl(SourceRange range) : DeclNode(AstKind::ErrorDecl, range) {}
};

} // namespace rhc
