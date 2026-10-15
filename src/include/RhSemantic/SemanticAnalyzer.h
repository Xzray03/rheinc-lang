#pragma once

#include "RhCore/DiagnosticEngine.h"
#include "RhCore/SourceManager.h"
#include "RhParser/Parser.h"
#include "RhSemantic/BuiltinRegistry.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/TypeResolver.h"

#include <unordered_map>
#include <unordered_set>

namespace rhc {

class SemanticAnalyzer {
public:
    SemanticAnalyzer(SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    bool analyze(TranslationUnitDecl* unit);

    SemanticContext& context() { return context_; }
    const SemanticContext& context() const { return context_; }

private:
    void collectDeclaration(DeclNode* decl);
    void analyzeDeclaration(DeclNode* decl);

    void collectFunction(FunctionDecl* decl);
    void analyzeFunction(FunctionDecl* decl);
    void collectGlobalVariable(GlobalVarDecl* decl);
    void analyzeGlobalVariable(GlobalVarDecl* decl);
    void collectStruct(StructDecl* decl);
    void collectEnum(EnumDecl* decl);
    void collectGeneric(GenericDecl* decl);
    void analyzeEnum(EnumDecl* decl);

    void analyzeStatement(StmtNode* stmt);
    void analyzeVariableStatement(VarDeclStmt* stmt);
    void analyzeIfStatement(IfStmt* stmt);
    void analyzeForStatement(ForStmt* stmt);
    void analyzeWhileStatement(WhileStmt* stmt);
    void analyzeDoWhileStatement(DoWhileStmt* stmt);
    void analyzeReturnStatement(ReturnStmt* stmt);
    void analyzeJaminStatement(JaminStmt* stmt);
    void analyzeJedaStatement(JedaStmt* stmt);
    void analyzeKeluarStatement(KeluarStmt* stmt);
    void analyzeDeclStatement(DeclStmt* stmt);
    void bindConstructorPattern(ConstructorPattern* pattern, Type* subjectType);

    Type* analyzeExpression(ExprNode* expr);
    Type* analyzeIdentifier(IdentifierExpr* expr);
    Type* analyzeCall(CallExpr* expr);
    Type* analyzeBinary(BinaryExpr* expr);
    Type* analyzeTernary(TernaryExpr* expr);
    Type* analyzeUnary(UnaryExpr* expr);
    Type* analyzeArrayLiteral(ArrayLiteralExpr* expr);
    Type* analyzeArrayIndex(ArrayIndexExpr* expr);
    Type* analyzeSlice(SliceExpr* expr);
    Type* analyzeMember(MemberExpr* expr);
    Type* analyzeLambda(LambdaExpr* expr);
    Type* typeFromExpression(ExprNode* expr);
    Type* tryResolveConstructorCall(CallExpr* expr, const std::string& name);
    void analyzePilihStatement(PilihStmt* stmt);
    void analyzeLabelStatement(LabelStmt* stmt);
    void analyzeGotoStatement(GotoStmt* stmt);
    void collectLabels(StmtNode* stmt);
    Type* commonType(Type* lhs, Type* rhs);

    Type* substitute(Type* type, const std::unordered_map<TypeVariableType*, Type*>& substitutions);
    bool unify(Type* pattern, Type* actual,
               std::unordered_map<TypeVariableType*, Type*>& substitutions);
    std::unordered_map<TypeVariableType*, Type*> instantiationMap(GenericInstantiationType* instantiation);
    std::vector<TypeVariableType*> typeParamsOf(Type* type);
    void pushExpectedType(Type* type) { expectedTypeStack_.push_back(type); }
    void popExpectedType() { expectedTypeStack_.pop_back(); }
    Type* expectedType() const { return expectedTypeStack_.empty() ? nullptr : expectedTypeStack_.back(); }
    void insertGlobalAllowingBuiltinShadow(Symbol* symbol);

    // ===== Kategori A: flow analysis =====
    // Definite-assignment tracking, keyed by Symbol* (flat; symbols are unique
    // per declaration, stale entries are unreachable after scope exit).
    void declareTracked(Symbol* symbol, bool assigned) { assigned_[symbol] = assigned; }
    void markAssigned(Symbol* symbol) { assigned_[symbol] = true; }
    bool isDefinitelyAssigned(Symbol* symbol) const;
    std::unordered_map<Symbol*, bool> assigned_;
    // Local `tetap` constants (their VarDeclStmt is not a DeclNode, so the
    // declaration cannot be recovered from Symbol::decl).
    std::unordered_set<Symbol*> constLocals_;
    unsigned suppressReadChecks_ = 0;
    // Return-path analysis: true when the statement cannot fall through.
    bool statementTerminates(StmtNode* stmt) const;
    void checkReturnPaths(StmtNode* body, Type* returnType, SourceRange range);

    void error(SourceRange range, const std::string& message);
    bool isLogicType(Type* type) const;
    bool isNumericType(Type* type) const;
    bool isAssignableTo(Type* source, Type* destination) const;

    SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
    SemanticContext context_;
    BuiltinRegistry builtins_;
    TypeResolver resolver_;

    Scope* currentScope_ = nullptr;
    FunctionType* currentFunctionType_ = nullptr;
    FunctionDecl* currentFunctionDecl_ = nullptr;
    unsigned loopDepth_ = 0;
    unsigned pilihDepth_ = 0;

    // Expected-type context, e.g. the declared type of a variable initializer.
    std::vector<Type*> expectedTypeStack_;
    // Stack of collectors for lambda return type inference (innermost = current lambda).
    std::vector<std::vector<Type*>> returnCollectors_;
    // Function symbols whose FunctionType carries type parameters.
    std::unordered_set<Symbol*> genericFunctions_;
    // Top-level generic templates: name -> GenericDecl (generic struct / cacahan_sum).
    std::unordered_map<std::string, GenericDecl*> genericTemplates_;
    // Sum types keyed by name, for constructor pattern resolution.
    std::unordered_map<std::string, SumType*> sumTypesByName_;
};

} // namespace rhc
