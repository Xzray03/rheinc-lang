#include "RhSemantic/SemanticContext.h"

#include <utility>

namespace rhc {

SemanticContext::SemanticContext() : globalScope_(nullptr) {}

Symbol* SemanticContext::createSymbol(SymbolKind kind, std::string name, Type* type,
                                      DeclNode* decl, SourceRange range, Scope* scope) {
    symbols_.push_back(std::make_unique<Symbol>(kind, std::move(name), type, decl, range, scope));
    return symbols_.back().get();
}

void SemanticContext::setExprType(const ExprNode* expr, Type* type) {
    exprTypes_[expr] = type;
}

Type* SemanticContext::getExprType(const ExprNode* expr) const {
    const auto it = exprTypes_.find(expr);
    return it == exprTypes_.end() ? nullptr : it->second;
}

void SemanticContext::setDeclSymbol(const DeclNode* decl, Symbol* symbol) {
    declSymbols_[decl] = symbol;
}

Symbol* SemanticContext::getDeclSymbol(const DeclNode* decl) const {
    const auto it = declSymbols_.find(decl);
    return it == declSymbols_.end() ? nullptr : it->second;
}

void SemanticContext::setResolvedType(const TypeReprNode* repr, Type* type) {
    resolvedTypes_[repr] = type;
}

Type* SemanticContext::getResolvedType(const TypeReprNode* repr) const {
    const auto it = resolvedTypes_.find(repr);
    return it == resolvedTypes_.end() ? nullptr : it->second;
}

} // namespace rhc
