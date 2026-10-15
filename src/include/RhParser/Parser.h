#pragma once

#include "RhCore/DiagnosticEngine.h"
#include "RhCore/SourceManager.h"
#include "RhParser/AstContext.h"
#include "RhParser/Decl.h"
#include "RhParser/Expr.h"
#include "RhParser/ParserTokenStream.h"
#include "RhParser/Stmt.h"
#include "RhParser/TypeRepr.h"
#include "RhPreprocessor/Preprocessor.h"

namespace rhc {

class Parser {
public:
    Parser(Preprocessor& preprocessor,
           SourceManager& sourceManager,
           DiagnosticEngine& diagnostics);

    TranslationUnitDecl* parseTranslationUnit();
    AstContext& ast() { return astContext_; }
    SourceRange makeRange(const Token& token) const;
    SourceRange makeRange(const Token& begin, const Token& end) const;
    std::string tokenText(const Token& token) const;
    bool isNameToken(const Token& token) const;

public:
    DeclNode* parseTopLevelDeclaration();
    FunctionDecl* parseFunctionLikeDeclaration(const Token& keywordToken, bool isProcedure, bool requireBody = true, bool allowConstraints = false);
    GenericDecl* parseGenericDeclaration(const Token& keywordToken);
    TraitConstraintClauses parseDimanaConstraints();
    EnumDecl* parseEnumDeclaration(const Token& keywordToken);
    GlobalVarDecl* parseGlobalVariableDeclaration(StorageModifier storage = StorageModifier::None);
    TestAnnotationDecl* parseTestAnnotationDeclaration();
    StructDecl* parseStructDeclaration(const Token& keywordToken);

    TypeReprNode* parseTypeRepr();
    TypeReprNode* parseTypeReprPrimary();
    std::vector<TypeArgumentRepr*> parseTypeArgumentList(bool angleBracket);

    CompoundStmt* parseBlock();
    StmtNode* parseStatement();
    StmtNode* parseVariableStatement(StorageModifier storage = StorageModifier::None);
    IfStmt* parseIfStatement();
    ForStmt* parseForStatement();
    WhileStmt* parseWhileStatement();
    DoWhileStmt* parseDoWhileStatement();
    BreakStmt* parseBreakStatement();
    ContinueStmt* parseContinueStatement();
    PilihStmt* parsePilihStatement();
    GotoStmt* parseGotoStatement();
    LabelStmt* parseLabelStatement(const Token& labelName);
    JaminStmt* parseJaminStatement();
    KeluarStmt* parseKeluarStatement();
    CaseClause* parseCaseClause();
    PatternNode* parseCasePattern();
    ConstructorPattern* parseConstructorPattern(const Token& constructorName);
    CompoundStmt* parseCaseBody();
    ReturnStmt* parseReturnStatement();
    StmtNode* parseExpressionStatement();

    ExprNode* parseExpression();
    ExprNode* parseSliceArgument();
    ExprNode* parseAssignmentExpression();
    ExprNode* parseTernaryExpression();
    ExprNode* parseBinaryExpression(int minPrecedence);
    ExprNode* parseUnaryExpression();
    ExprNode* parsePostfixExpression();
    ExprNode* parsePrimaryExpression();
    ExprNode* parseTypeExpression();
    ExprNode* parseIdentifierOrCallExpression();
    bool genericTypeAhead();
    ExprNode* parseCallExpression(ExprNode* callee, SourceRange startRange);

    void errorExpected(const char* what);
    void skipToTopLevelSynchronization();

    Preprocessor& preprocessor_;
    SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
    ParserTokenStream tokens_;
    AstContext astContext_;
};

} // namespace rhc
