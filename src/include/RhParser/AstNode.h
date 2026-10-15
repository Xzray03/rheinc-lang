#pragma once

#include "RhCore/SourceLocation.h"
#include "RhLexer/TokenKind.h"

#include <vector>
#include <string>
#include <utility>
#include <cstdint>

namespace rhc {

enum class StorageModifier {
    None,
    TaskLocal,
    TaskGlobal,
};

// Trait constraint clauses: type-variable name -> required trait names
// (e.g. `dimana T: Terbandingkan, E: Terpisah`, or inline `generik T: Trait`).
using TraitConstraintClauses = std::vector<std::pair<std::string, std::vector<std::string>>>;

enum class AstKind {
    TranslationUnit,
    FunctionDecl,
    ProcedureDecl,
    ParamDecl,
    StructDecl,
    StructField,
    GenericDecl,
    EnumDecl,
    EnumField,
    GlobalVarDecl,

    CompoundStmt,
    DeclStmt,
    VarDeclStmt,
    IfStmt,
    ForStmt,
    WhileStmt,
    DoWhileStmt,
    BreakStmt,
    ContinueStmt,
    PilihStmt,
    CaseClause,
    ExpressionPattern,
    ConstructorPattern,
    PatternBinding,
    GotoStmt,
    LabelStmt,
    JaminStmt,
    JedaStmt,
    KeluarStmt,
    ReturnStmt,
    ExprStmt,

    BuiltinTypeRepr,
    NamedTypeRepr,
    TypeArgumentRepr,
    CallTypeRepr,
    ArrayTypeRepr,
    PointerTypeRepr,
    FunctionPointerTypeRepr,
    ErrorTypeRepr,

    IdentifierExpr,
    IntegerLiteralExpr,
    CharLiteralExpr,
    BoolLiteralExpr,
    NullLiteralExpr,
    StringLiteralExpr,
    ArrayLiteralExpr,
    CallExpr,
    BinaryExpr,
    UnaryExpr,
    TernaryExpr,
    MemberExpr,
    ArrayIndexExpr,
    SliceExpr,
    NamedArgumentExpr,
    LambdaExpr,
    TypeExpr,
    ErrorExpr,
    ErrorStmt,
    ErrorDecl,
    TestAnnotationDecl,
};

struct AstNode {
    AstKind kind;
    SourceRange range;

    explicit AstNode(AstKind nodeKind, SourceRange nodeRange = {})
        : kind(nodeKind), range(nodeRange) {}
    virtual ~AstNode() = default;
};

} // namespace rhc
