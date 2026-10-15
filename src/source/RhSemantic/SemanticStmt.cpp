#include <algorithm>
#include "RhSemantic/SemanticAnalyzer.h"

namespace rhc {

void SemanticAnalyzer::analyzeStatement(StmtNode* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
    case AstKind::CompoundStmt: {
        Scope* previous = currentScope_;
        currentScope_ = currentScope_->createChild();
        for (StmtNode* child : static_cast<CompoundStmt*>(stmt)->statements) {
            analyzeStatement(child);
        }
        currentScope_ = previous;
        break;
    }
    case AstKind::VarDeclStmt:
        analyzeVariableStatement(static_cast<VarDeclStmt*>(stmt));
        break;
    case AstKind::IfStmt:
        analyzeIfStatement(static_cast<IfStmt*>(stmt));
        break;
    case AstKind::ForStmt:
        analyzeForStatement(static_cast<ForStmt*>(stmt));
        break;
    case AstKind::WhileStmt:
        analyzeWhileStatement(static_cast<WhileStmt*>(stmt));
        break;
    case AstKind::DoWhileStmt:
        analyzeDoWhileStatement(static_cast<DoWhileStmt*>(stmt));
        break;
    case AstKind::BreakStmt:
        if (loopDepth_ == 0 && pilihDepth_ == 0) error(stmt->range, "berhenti outside loop");
        break;
    case AstKind::ContinueStmt:
        if (loopDepth_ == 0) error(stmt->range, "lanjut outside loop");
        break;
    case AstKind::PilihStmt:
        analyzePilihStatement(static_cast<PilihStmt*>(stmt));
        break;
    case AstKind::LabelStmt:
        analyzeLabelStatement(static_cast<LabelStmt*>(stmt));
        break;
    case AstKind::GotoStmt:
        analyzeGotoStatement(static_cast<GotoStmt*>(stmt));
        break;
    case AstKind::ReturnStmt:
        analyzeReturnStatement(static_cast<ReturnStmt*>(stmt));
        break;
    case AstKind::JaminStmt:
        analyzeJaminStatement(static_cast<JaminStmt*>(stmt));
        break;
    case AstKind::JedaStmt:
        analyzeJedaStatement(static_cast<JedaStmt*>(stmt));
        break;
    case AstKind::KeluarStmt:
        analyzeKeluarStatement(static_cast<KeluarStmt*>(stmt));
        break;
    case AstKind::DeclStmt:
        analyzeDeclStatement(static_cast<DeclStmt*>(stmt));
        break;
    case AstKind::ExprStmt:
        analyzeExpression(static_cast<ExprStmt*>(stmt)->expression);
        break;
    default:
        error(stmt->range, "unsupported statement in semantic v1");
        break;
    }
}

void SemanticAnalyzer::analyzeVariableStatement(VarDeclStmt* stmt) {
    Type* declaredType = stmt->type ? resolver_.resolve(stmt->type) : nullptr;
    if (declaredType) pushExpectedType(declaredType);
    Type* initializerType = stmt->initializer ? analyzeExpression(stmt->initializer) : nullptr;
    if (declaredType) popExpectedType();

    if (stmt->isConst && !stmt->initializer) {
        error(stmt->range, "constant variable requires an initializer");
    }
    if (!stmt->type && !stmt->initializer) {
        error(stmt->range, "variable needs a type or initializer");
    }
    if (declaredType && initializerType && !isAssignableTo(initializerType, declaredType)) {
        error(stmt->range, "type mismatch in variable initializer");
    }

    Type* variableType = declaredType ? declaredType : initializerType;
    if (!variableType) variableType = context_.types().getErrorType();

    Symbol* symbol = context_.createSymbol(
        SymbolKind::LocalVar, stmt->name, variableType, nullptr, stmt->range, currentScope_);
    if (!currentScope_->insert(symbol)) {
        // Allow a real declaration to replace an arrow-capture placeholder.
        Symbol* existing = currentScope_->lookupLocal(stmt->name);
        if (existing && existing->decl == nullptr && existing->kind == SymbolKind::LocalVar) {
            currentScope_->insertOrReplace(symbol);
        } else {
            error(stmt->range, "redefinition of name: " + stmt->name);
        }
    }
}

void SemanticAnalyzer::analyzeIfStatement(IfStmt* stmt) {
    Type* conditionType = analyzeExpression(stmt->condition);
    if (conditionType && !isLogicType(conditionType)) {
        error(stmt->condition->range, "if condition must be logika");
    }
    analyzeStatement(stmt->thenBranch);
    if (stmt->elseBranch) {
        analyzeStatement(stmt->elseBranch);
    }
}


void SemanticAnalyzer::analyzeForStatement(ForStmt* stmt) {
    Scope* previousScope = currentScope_;
    currentScope_ = currentScope_->createChild();
    ++loopDepth_;

    if (stmt->isForEach) {
        if (stmt->stepValue) {
            Type* stepType = analyzeExpression(stmt->stepValue);
            if (!isNumericType(stepType) && stepType != context_.types().getErrorType()) {
                error(stmt->stepValue->range, "for-each step must be numeric");
            }
        }
        Type* iterableType = analyzeExpression(stmt->iterable);
        Type* elementType = context_.types().getErrorType();
        if (auto* arrayType = dynamic_cast<ArrayType*>(iterableType)) {
            elementType = arrayType->elementType;
        } else if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(iterableType)) {
            std::vector<TypeVariableType*> params;
            if (auto* object = dynamic_cast<BuiltinObjectType*>(instantiation->generic)) {
                params = object->typeParams;
            } else if (auto* sum = dynamic_cast<SumType*>(instantiation->generic)) {
                params = sum->typeParams;
            }
            for (std::size_t i = 0; i < params.size(); ++i) {
                if (params[i]->name != "T") continue;
                for (const auto& [argumentName, argumentType] : instantiation->arguments) {
                    if (argumentName == "T") elementType = argumentType;
                }
                if (elementType == context_.types().getErrorType() &&
                    i < instantiation->arguments.size()) {
                    elementType = instantiation->arguments[i].second;
                }
                break;
            }
            if (elementType == context_.types().getErrorType()) {
                error(stmt->iterable->range, "cannot determine for-each element type");
            }
        } else if (iterableType != context_.types().getErrorType()) {
            error(stmt->iterable->range, "for-each expects an array or generator value");
        }

        for (const std::string& variable : stmt->loopVariables) {
            Symbol* symbol = context_.createSymbol(
                SymbolKind::LocalVar, variable, elementType, nullptr, stmt->range, currentScope_);
            if (!currentScope_->insert(symbol)) {
                error(stmt->range, "redefinition of name: " + variable);
            }
        }
    } else {
        Type* startType = analyzeExpression(stmt->startValue);
        Type* endType = analyzeExpression(stmt->endValue);
        if (!isNumericType(startType) || !isNumericType(endType)) {
            error(stmt->range, "for range bounds must be numeric");
        }
        if (stmt->stepValue) {
            Type* stepType = analyzeExpression(stmt->stepValue);
            if (!isNumericType(stepType)) {
                error(stmt->stepValue->range, "for step must be numeric");
            }
        }
        Type* loopType = context_.types().getBuiltinType(BuiltinTypeKind::Bilangan);
        for (const std::string& variable : stmt->loopVariables) {
            Symbol* symbol = context_.createSymbol(
                SymbolKind::LocalVar, variable, loopType, nullptr, stmt->range, currentScope_);
            if (!currentScope_->insert(symbol)) {
                error(stmt->range, "redefinition of name: " + variable);
            }
        }
    }

    analyzeStatement(stmt->body);
    --loopDepth_;
    currentScope_ = previousScope;
}

void SemanticAnalyzer::analyzeWhileStatement(WhileStmt* stmt) {
    Type* conditionType = analyzeExpression(stmt->condition);
    if (!isLogicType(conditionType) && conditionType != context_.types().getErrorType()) {
        error(stmt->condition->range, "while condition must be logika");
    }
    ++loopDepth_;
    analyzeStatement(stmt->body);
    --loopDepth_;
}

void SemanticAnalyzer::analyzeDoWhileStatement(DoWhileStmt* stmt) {
    ++loopDepth_;
    analyzeStatement(stmt->body);
    --loopDepth_;
    Type* conditionType = analyzeExpression(stmt->condition);
    if (!isLogicType(conditionType)) {
        error(stmt->condition->range, "do-while condition must be logika");
    }
}



void SemanticAnalyzer::collectLabels(StmtNode* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
    case AstKind::CompoundStmt:
        for (StmtNode* child : static_cast<CompoundStmt*>(stmt)->statements) collectLabels(child);
        break;
    case AstKind::LabelStmt: {
        auto* label = static_cast<LabelStmt*>(stmt);
        Symbol* symbol = context_.createSymbol(SymbolKind::LocalVar, label->label,
                                               context_.types().getBuiltinType(BuiltinTypeKind::Hampa),
                                               nullptr, stmt->range, currentScope_);
        if (!currentScope_->insert(symbol)) {
            error(stmt->range, "redefinition of label: " + label->label);
        }
        collectLabels(label->statement);
        break;
    }
    case AstKind::IfStmt: {
        auto* ifStmt = static_cast<IfStmt*>(stmt);
        collectLabels(ifStmt->thenBranch);
        if (ifStmt->elseBranch) collectLabels(ifStmt->elseBranch);
        break;
    }
    case AstKind::ForStmt: {
        auto* loop = static_cast<ForStmt*>(stmt);
        collectLabels(loop->body);
        break;
    }
    case AstKind::WhileStmt:
        collectLabels(static_cast<WhileStmt*>(stmt)->body);
        break;
    case AstKind::DoWhileStmt:
        collectLabels(static_cast<DoWhileStmt*>(stmt)->body);
        break;
    case AstKind::PilihStmt: {
        auto* pilih = static_cast<PilihStmt*>(stmt);
        for (CaseClause* clause : pilih->clauses) collectLabels(clause->body);
        break;
    }
    default:
        break;
    }
}

void SemanticAnalyzer::analyzeLabelStatement(LabelStmt* stmt) {
    analyzeStatement(stmt->statement);
}

void SemanticAnalyzer::analyzeGotoStatement(GotoStmt* stmt) {
    if (!currentScope_->lookup(stmt->label)) {
        error(stmt->range, "unknown label: " + stmt->label);
    }
}

void SemanticAnalyzer::analyzePilihStatement(PilihStmt* stmt) {
    ++pilihDepth_;
    Type* subjectType = analyzeExpression(stmt->subject);

    for (CaseClause* clause : stmt->clauses) {
        Scope* previousScope = currentScope_;
        currentScope_ = currentScope_->createChild();
        for (PatternNode* pattern : clause->patterns) {
            if (pattern->kind == AstKind::ExpressionPattern) {
                Type* patternType = analyzeExpression(
                    static_cast<ExpressionPattern*>(pattern)->expression);
                if (!isAssignableTo(patternType, subjectType) &&
                    !isAssignableTo(subjectType, patternType)) {
                    error(pattern->range, "case pattern type does not match pilih subject");
                }
            } else if (pattern->kind == AstKind::ConstructorPattern) {
                bindConstructorPattern(static_cast<ConstructorPattern*>(pattern), subjectType);
            } else {
                error(pattern->range, "unsupported case pattern in semantic v1");
            }
        }
        analyzeStatement(clause->body);
        currentScope_ = previousScope;
    }
    --pilihDepth_;
}

void SemanticAnalyzer::analyzeReturnStatement(ReturnStmt* stmt) {
    if (!currentFunctionType_) {
        error(stmt->range, "return outside function");
        return;
    }
    Type* expected = currentFunctionType_->returnType;
    Type* hampa = context_.types().getBuiltinType(BuiltinTypeKind::Hampa);

    if (!returnCollectors_.empty()) {
        // Lambda return type inference in progress; record and skip checks.
        if (!stmt->value) {
            returnCollectors_.back().push_back(hampa);
            return;
        }
        Type* inferenceExpected =
            expected == context_.types().getErrorType() ? nullptr : expected;
        if (inferenceExpected) pushExpectedType(inferenceExpected);
        Type* valueType = analyzeExpression(stmt->value);
        if (inferenceExpected) popExpectedType();
        returnCollectors_.back().push_back(valueType);
        return;
    }

    if (!stmt->value) {
        if (expected != hampa) {
            error(stmt->range, "procedure-style return in function with return value");
        }
        return;
    }

    pushExpectedType(expected);
    Type* valueType = analyzeExpression(stmt->value);
    popExpectedType();
    if (expected == hampa) {
        error(stmt->value->range, "function returning hampa cannot return a value");
        return;
    }
    if (!isAssignableTo(valueType, expected)) {
        error(stmt->value->range, "return type mismatch");
    }
}

void SemanticAnalyzer::analyzeJaminStatement(JaminStmt* stmt) {
    analyzeStatement(stmt->body);
}

void SemanticAnalyzer::analyzeJedaStatement(JedaStmt* stmt) {
    bool inCoroutine = currentFunctionDecl_ &&
        (std::find(currentFunctionDecl_->modifiers.begin(),
                   currentFunctionDecl_->modifiers.end(),
                   FunctionModifier::Coroutine) != currentFunctionDecl_->modifiers.end() ||
         std::find(currentFunctionDecl_->modifiers.begin(),
                   currentFunctionDecl_->modifiers.end(),
                   FunctionModifier::Generator) != currentFunctionDecl_->modifiers.end());
    if (!inCoroutine) {
        error(stmt->range, "jeda outside coroutine or generator");
    }
    if (stmt->untilCondition) {
        Type* conditionType = analyzeExpression(stmt->untilCondition);
        if (!isLogicType(conditionType)) {
            error(stmt->untilCondition->range, "jeda condition must be logika");
        }
    }
}

void SemanticAnalyzer::analyzeKeluarStatement(KeluarStmt* stmt) {
    if (stmt->value) {
        analyzeExpression(stmt->value);
    }
}

void SemanticAnalyzer::analyzeDeclStatement(DeclStmt* stmt) {
    collectDeclaration(stmt->declaration);
    analyzeDeclaration(stmt->declaration);
}

void SemanticAnalyzer::bindConstructorPattern(ConstructorPattern* pattern, Type* subjectType) {
    Type* errorType = context_.types().getErrorType();

    if (subjectType == errorType) {
        // Permissive: the pilih subject failed to type; bind as error-typed
        // so the case body still gets checked loosely.
        for (PatternBinding* binding : pattern->bindings) {
            Symbol* symbol = context_.createSymbol(
                SymbolKind::LocalVar, binding->name, errorType, nullptr, binding->range,
                currentScope_);
            currentScope_->insert(symbol);
            if (binding->nestedPattern) {
                bindConstructorPattern(static_cast<ConstructorPattern*>(binding->nestedPattern), errorType);
            }
        }
        return;
    }

    SumType* sum = nullptr;
    std::unordered_map<TypeVariableType*, Type*> substitutions;
    if (auto* direct = dynamic_cast<SumType*>(subjectType)) {
        sum = direct;
    } else if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(subjectType)) {
        sum = dynamic_cast<SumType*>(instantiation->generic);
        if (sum) substitutions = instantiationMap(instantiation);
    }
    if (!sum) {
        error(pattern->range, "constructor pattern requires a cacahan / cacahan_sum subject");
        return;
    }

    const SumType::Case* foundCase = sum->findCase(pattern->constructorName);
    if (!foundCase) {
        error(pattern->range, "unknown case: " + pattern->constructorName);
        return;
    }
    if (pattern->bindings.size() > foundCase->fields.size()) {
        error(pattern->range,
              "too many fields in pattern for case: " + pattern->constructorName);
        return;
    }
    if (!pattern->bindings.empty() && foundCase->fields.empty()) {
        error(pattern->range, "case has no fields: " + pattern->constructorName);
        return;
    }

    // Bindings are positional (Rust-style): the binding name declares a new
    // variable and does not have to match the field name.
    for (std::size_t i = 0; i < pattern->bindings.size(); ++i) {
        PatternBinding* binding = pattern->bindings[i];
        const SumType::Field* field = &foundCase->fields[i];
        Type* fieldType = substitute(field->type, substitutions);
        if (binding->type) {
            Type* declared = resolver_.resolve(binding->type);
            if (!isAssignableTo(fieldType, declared)) {
                error(binding->range, "pattern binding type mismatch");
            }
        }
        Symbol* symbol = context_.createSymbol(
            SymbolKind::LocalVar, binding->name, fieldType, nullptr, binding->range,
            currentScope_);
        if (!currentScope_->insert(symbol)) {
            error(binding->range, "redefinition of name: " + binding->name);
        }
        if (binding->nestedPattern) {
            bindConstructorPattern(static_cast<ConstructorPattern*>(binding->nestedPattern), fieldType);
        }
    }
}

} // namespace rhc
