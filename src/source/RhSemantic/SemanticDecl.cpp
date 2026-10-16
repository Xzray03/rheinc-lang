#include "RhSemantic/Layout.h"
#include <cstdio>
#include "RhSemantic/ConstExpr.h"
#include <algorithm>
#include "RhSemantic/SemanticAnalyzer.h"

#include <utility>

namespace rhc {

void SemanticAnalyzer::collectDeclaration(DeclNode* decl) {
    if (!decl) return;
    switch (decl->kind) {
    case AstKind::FunctionDecl:
    case AstKind::ProcedureDecl:
        collectFunction(static_cast<FunctionDecl*>(decl));
        break;
    case AstKind::GlobalVarDecl:
        collectGlobalVariable(static_cast<GlobalVarDecl*>(decl));
        break;
    case AstKind::StructDecl:
        collectStruct(static_cast<StructDecl*>(decl));
        break;
    case AstKind::EnumDecl:
        collectEnum(static_cast<EnumDecl*>(decl));
        break;
    case AstKind::GenericDecl:
        collectGeneric(static_cast<GenericDecl*>(decl));
        break;
    default:
        error(decl->range, "unsupported top-level declaration in semantic v1");
        break;
    }
}

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

void SemanticAnalyzer::collectFunction(FunctionDecl* decl) {
    std::vector<FunctionParamType> params;
    for (ParamDecl* param : decl->parameters) {
        Type* type = resolver_.resolve(param->type);
        params.push_back(FunctionParamType{param->name, type});
    }

    Type* returnType = context_.types().getBuiltinType(BuiltinTypeKind::Hampa);
    if (!decl->isProcedure) {
        if (decl->returnType) {
            returnType = resolver_.resolve(decl->returnType);
        } else {
            error(decl->range, "function requires a return type");
        }
    }

    auto* functionType = context_.types().getFunctionType(std::move(params), returnType, false,
                                                          resolver_.currentTypeVariables());
    Symbol* existing = context_.globalScope().lookupLocal(decl->name);
    if (existing && isBuiltinSymbol(*existing)) {
        // User declarations may shadow builtins such as `gabung`.
        Symbol* symbol = context_.createSymbol(
            decl->isProcedure ? SymbolKind::Procedure : SymbolKind::Function,
            decl->name, functionType, decl, decl->range, &context_.globalScope());
        context_.globalScope().insertOrReplace(symbol);
        context_.setDeclSymbol(decl, symbol);
        context_.module().functions.push_back(symbol);
        return;
    }
    if (existing && existing->type == functionType) {
        auto* existingDecl = dynamic_cast<FunctionDecl*>(existing->decl);
        if (existingDecl && !existingDecl->body && decl->body) {
            existing->decl = decl;
            context_.setDeclSymbol(decl, existing);
            return;
        }
        if (existingDecl && existingDecl->body && !decl->body) {
            context_.setDeclSymbol(decl, existing);
            return;
        }
        error(decl->range, "redefinition of name: " + decl->name);
        context_.setDeclSymbol(decl, existing);
        return;
    }
    if (existing) {
        error(decl->range, "redefinition of name: " + decl->name);
    }

    Symbol* symbol = context_.createSymbol(
        decl->isProcedure ? SymbolKind::Procedure : SymbolKind::Function,
        decl->name, functionType, decl, decl->range, &context_.globalScope());
    context_.globalScope().insert(symbol);
    context_.setDeclSymbol(decl, symbol);
    context_.module().functions.push_back(symbol);
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

void SemanticAnalyzer::collectGlobalVariable(GlobalVarDecl* decl) {
    Type* type = decl->type ? resolver_.resolve(decl->type) : nullptr;
    Symbol* symbol = context_.createSymbol(
        SymbolKind::GlobalVar, decl->name, type, decl, decl->range, &context_.globalScope());
    Symbol* existing = context_.globalScope().lookupLocal(decl->name);
    if (existing && isBuiltinSymbol(*existing)) {
        context_.globalScope().insertOrReplace(symbol);
    } else if (!context_.globalScope().insert(symbol)) {
        error(decl->range, "redefinition of name: " + decl->name);
    }
    if (decl->isConst && !decl->initializer) {
        error(decl->range, "konstanta requires an initializer: " + decl->name);
    }
    context_.setDeclSymbol(decl, symbol);
    context_.module().globals.push_back(symbol);
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

void SemanticAnalyzer::collectStruct(StructDecl* decl) {
    auto* structType = context_.types().createStructType(decl);
    structType->typeParams = resolver_.currentTypeVariables();
    Symbol* symbol = context_.createSymbol(
        SymbolKind::Struct, decl->name, structType, decl, decl->range, &context_.globalScope());
    Symbol* existingStruct = context_.globalScope().lookupLocal(decl->name);
    if (existingStruct && isBuiltinSymbol(*existingStruct)) {
        context_.globalScope().insertOrReplace(symbol);
    } else if (!context_.globalScope().insert(symbol)) {
        error(decl->range, "redefinition of name: " + decl->name);
    }
    context_.setDeclSymbol(decl, symbol);
    context_.module().structs.push_back(symbol);

    for (StructField* field : decl->fields) {
        Type* fieldType = resolver_.resolve(field->type);
        Symbol* fieldSymbol = context_.createSymbol(
            SymbolKind::StructField, field->name, fieldType, field, field->range,
            &context_.globalScope());
        structType->fields.push_back(fieldSymbol);
        context_.setDeclSymbol(field, fieldSymbol);
    }
}

void SemanticAnalyzer::collectEnum(EnumDecl* decl) {
    bool hasPayload = false;
    for (EnumField* field : decl->fields) {
        if (!field->fields.empty()) {
            hasPayload = true;
            break;
        }
    }

    // Phase 1: create the type and register the symbol so recursive
    // references (e.g. CABANG(anak: Opsi(POHON))) can resolve.
    auto* sumType = context_.types().createSumType(
        decl->name, decl, !decl->isSum && !hasPayload, resolver_.currentTypeVariables());
    for (EnumField* field : decl->fields) {
        SumType::Case c;
        c.name = field->name;
        sumType->cases.push_back(std::move(c));
    }

    Symbol* symbol = context_.createSymbol(
        SymbolKind::Enum, decl->name, sumType, decl, decl->range, &context_.globalScope());
    Symbol* existing = context_.globalScope().lookupLocal(decl->name);
    if (existing && isBuiltinSymbol(*existing)) {
        context_.globalScope().insertOrReplace(symbol);
    } else if (!context_.globalScope().insert(symbol)) {
        error(decl->range, "redefinition of name: " + decl->name);
    }
    context_.setDeclSymbol(decl, symbol);
    sumTypesByName_[decl->name] = sumType;

    // Phase 2: resolve field types and aliases now that the symbol exists.
    for (std::size_t i = 0; i < decl->fields.size() && i < sumType->cases.size(); ++i) {
        EnumField* field = decl->fields[i];
        SumType::Case& c = sumType->cases[i];
        for (const auto& [fieldName, fieldTypeRepr] : field->fields) {
            c.fields.push_back(SumType::Field{fieldName, resolver_.resolve(fieldTypeRepr)});
        }
        c.aliasValue = field->value;
        if (field->value && !hasPayload) {
            // Alias may denote another type, e.g. OPSI_BILANGAN = cacahan_sum(Opsi(...)).
            if (Type* aliasType = typeFromExpression(field->value)) {
                c.aliasType = aliasType;
            }
        }
    }

    if (!hasPayload) {
        // Plain enum: cases are constants usable as plain identifiers.
        for (const SumType::Case& c : sumType->cases) {
            Type* constantType = c.aliasType ? c.aliasType : static_cast<Type*>(sumType);
            Symbol* constant = context_.createSymbol(
                SymbolKind::EnumConstant, c.name, constantType, nullptr, decl->range,
                &context_.globalScope());
            insertGlobalAllowingBuiltinShadow(constant);
        }
    }
}

void SemanticAnalyzer::collectGeneric(GenericDecl* decl) {
    resolver_.pushTypeVariables(decl->typeParameters);
    collectDeclaration(decl->declaration);

    // Kategori C: propagate trait constraints onto the generic type. Must run
    // while the type-variable scope is still open. Constraints are read from
    // the nested declaration (FunctionDecl/EnumDecl carry them from `dimana`).
    const TraitConstraintClauses* constraintClauses = nullptr;
    if (auto* f = dynamic_cast<FunctionDecl*>(decl->declaration)) {
        constraintClauses = &f->constraints;
    } else if (auto* e = dynamic_cast<EnumDecl*>(decl->declaration)) {
        constraintClauses = &e->constraints;
    }
    if (getenv("RHC_DEBUG_TRAIT")) {
        fprintf(stderr, "[generic] nested constraints=%zu typeParams=%zu\n",
                constraintClauses ? constraintClauses->size() : 99,
                decl->typeParameters.size());
    }
    if (constraintClauses && !constraintClauses->empty()) {
        std::string nestedName;
        if (auto* s = dynamic_cast<StructDecl*>(decl->declaration)) nestedName = s->name;
        else if (auto* e = dynamic_cast<EnumDecl*>(decl->declaration)) nestedName = e->name;
        else if (auto* f = dynamic_cast<FunctionDecl*>(decl->declaration)) nestedName = f->name;
        Symbol* nestedSymbol =
            nestedName.empty() ? nullptr : context_.globalScope().lookupLocal(nestedName);
        if (nestedSymbol && nestedSymbol->type) {
            auto vars = resolver_.currentTypeVariables();
            auto applyConstraints = [&](auto* target) {
                for (const auto& [tvName, traits] : *constraintClauses) {
                    for (auto* v : vars) {
                        if (v->name == tvName) {
                            target->constraints.emplace_back(v, traits);
                            break;
                        }
                    }
                }
            };
            if (auto* fn = dynamic_cast<FunctionType*>(nestedSymbol->type)) applyConstraints(fn);
            else if (auto* sum = dynamic_cast<SumType*>(nestedSymbol->type)) applyConstraints(sum);
            else if (auto* st = dynamic_cast<StructType*>(nestedSymbol->type)) applyConstraints(st);
        }
    }
    resolver_.popTypeVariables();

    std::string name;
    if (auto* structure = dynamic_cast<StructDecl*>(decl->declaration)) {
        name = structure->name;
    } else if (auto* enumeration = dynamic_cast<EnumDecl*>(decl->declaration)) {
        name = enumeration->name;
    } else if (auto* function = dynamic_cast<FunctionDecl*>(decl->declaration)) {
        name = function->name;
    }
    if (name.empty()) return;

    Symbol* symbol = context_.globalScope().lookupLocal(name);
    if (!symbol || !symbol->type) return;
    if (auto* functionType = dynamic_cast<FunctionType*>(symbol->type);
        functionType && !functionType->typeParams.empty()) {
        genericFunctions_.insert(symbol);
    } else {
        genericTemplates_[name] = decl;
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
