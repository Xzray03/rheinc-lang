#pragma once

#include "RhParser/Decl.h"
#include "RhParser/Expr.h"
#include "RhParser/TypeRepr.h"
#include "RhSemantic/SemanticModule.h"
#include "RhSemantic/Scope.h"
#include "RhSemantic/TypeContext.h"

#include <deque>
#include <memory>
#include <unordered_map>

namespace rhc {

class SemanticContext {
public:
    SemanticContext();

    TypeContext& types() { return typeContext_; }
    Scope& globalScope() { return globalScope_; }
    SemanticModule& module() { return module_; }

    Symbol* createSymbol(SymbolKind kind, std::string name, Type* type,
                         DeclNode* decl, SourceRange range, Scope* scope);

    void setExprType(const ExprNode* expr, Type* type);
    Type* getExprType(const ExprNode* expr) const;

    void setDeclSymbol(const DeclNode* decl, Symbol* symbol);
    Symbol* getDeclSymbol(const DeclNode* decl) const;

    void setResolvedType(const TypeReprNode* repr, Type* type);
    Type* getResolvedType(const TypeReprNode* repr) const;

private:
    TypeContext typeContext_;
    Scope globalScope_;
    SemanticModule module_;
    std::deque<std::unique_ptr<Symbol>> symbols_;

    std::unordered_map<const ExprNode*, Type*> exprTypes_;
    std::unordered_map<const DeclNode*, Symbol*> declSymbols_;
    std::unordered_map<const TypeReprNode*, Type*> resolvedTypes_;
};

} // namespace rhc
