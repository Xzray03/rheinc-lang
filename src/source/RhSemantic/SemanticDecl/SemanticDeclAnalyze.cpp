#include "RhSemantic/Layout.h"
#include <cstdio>
#include "RhSemantic/ConstExpr.h"
#include <algorithm>
#include "RhSemantic/SemanticAnalyzer.h"

#include <utility>

namespace rhc {

void SemanticAnalyzer::analyzeDeclaration(DeclNode* decl) {
    if (!decl) return;
    switch (decl->kind) {
    case AstKind::FunctionDecl:
    case AstKind::ProcedureDecl:
        analyzeFunction(static_cast<FunctionDecl*>(decl));
        break;
    case AstKind::GlobalVarDecl:
        analyzeGlobalVariable(static_cast<GlobalVarDecl*>(decl));
        break;
    case AstKind::StructDecl: {
        // E-5: reject structs with no finite size (direct or generic recursion
        // by value); pointer indirection breaks the cycle.
        Symbol* symbol = context_.getDeclSymbol(decl);
        if (symbol && symbol->type) {
            bool infinite = false;
            rhc::computeLayout(symbol->type, &infinite);
            if (infinite) {
                error(decl->range, "struktur has infinite size (contains itself by value): " +
                                       static_cast<StructDecl*>(decl)->name);
            }
        }
        break;
    }
    case AstKind::EnumDecl:
        analyzeEnum(static_cast<EnumDecl*>(decl));
        break;
    case AstKind::GenericDecl:
        analyzeDeclaration(static_cast<GenericDecl*>(decl)->declaration);
        break;
    default:
        break;
    }
}

void SemanticAnalyzer::analyzeFunction(FunctionDecl* decl) {
    Symbol* functionSymbol = context_.getDeclSymbol(decl);
    if (!functionSymbol) return;
    auto* functionType = dynamic_cast<FunctionType*>(functionSymbol->type);
    if (!functionType) return;

    Scope* previousScope = currentScope_;
    FunctionDecl* previousFunctionDecl = currentFunctionDecl_;
    currentScope_ = context_.globalScope().createChild();
    currentFunctionType_ = functionType;
    currentFunctionDecl_ = decl;

    for (std::size_t i = 0; i < decl->parameters.size(); ++i) {
        ParamDecl* param = decl->parameters[i];
        Type* paramType = functionType->params[i].type;
        Symbol* paramSymbol = context_.createSymbol(
            SymbolKind::Param, param->name, paramType, param, param->range, currentScope_);
        if (!currentScope_->insert(paramSymbol)) {
            error(param->range, "redefinition of parameter: " + param->name);
        }
        context_.setDeclSymbol(param, paramSymbol);
    }

    if (decl->body) {
        collectLabels(decl->body);
        analyzeStatement(decl->body);
    }

    // Prototypes have no body; generators/coroutines are (possibly infinite)
    // streams and may legitimately never execute kembalikan.
    const bool isStream =
        std::find(decl->modifiers.begin(), decl->modifiers.end(),
                  FunctionModifier::Generator) != decl->modifiers.end() ||
        std::find(decl->modifiers.begin(), decl->modifiers.end(),
                  FunctionModifier::Coroutine) != decl->modifiers.end();
    if (decl->body && !isStream) {
        checkReturnPaths(decl->body, functionType->returnType, decl->range);
    }
    currentFunctionType_ = nullptr;
    currentFunctionDecl_ = previousFunctionDecl;
    currentScope_ = previousScope;
}

void SemanticAnalyzer::analyzeGlobalVariable(GlobalVarDecl* decl) {
    Symbol* symbol = context_.getDeclSymbol(decl);
    if (!symbol) return;
    if (symbol->type) pushExpectedType(symbol->type);
    Type* initializerType = decl->initializer ? analyzeExpression(decl->initializer) : nullptr;
    if (symbol->type) popExpectedType();
    if (!decl->type) {
        if (!initializerType) {
            error(decl->range, "global variable needs a type or initializer");
            return;
        }
        symbol->type = initializerType;
        return;
    }
    if (initializerType && !isAssignableTo(initializerType, symbol->type)) {
        error(decl->range, "type mismatch in global variable initializer");
    }
}

void SemanticAnalyzer::analyzeEnum(EnumDecl* decl) {
    Symbol* symbol = context_.getDeclSymbol(decl);
    auto* sumType = symbol ? dynamic_cast<SumType*>(symbol->type) : nullptr;
    if (!sumType) return;

    // Kategori B: evaluate plain-enum constant values (auto-incrementing from
    // the previous case) so every case is numbered and ready for codegen.
    ConstTable table;
    std::int64_t nextValue = 0;
    for (std::size_t i = 0; i < decl->fields.size() && i < sumType->cases.size(); ++i) {
        EnumField* field = decl->fields[i];
        SumType::Case& c = sumType->cases[i];
        if (c.aliasType) continue; // type alias case: no constant value
        if (field->value) {
            if (auto value = evaluateConstInt(field->value, table)) {
                c.enumValue = *value;
            } else {
                error(field->range, "enum value must be a constant integer");
                c.enumValue = nextValue;
            }
        } else {
            c.enumValue = nextValue;
        }
        c.hasEnumValue = true;
        table[field->name] = c.enumValue;
        nextValue = c.enumValue + 1;
    }
}

} // namespace rhc
