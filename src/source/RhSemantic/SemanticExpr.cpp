#include <cstdio>
#include "RhSemantic/Traits.h"
#include <algorithm>
#include "RhSemantic/SemanticAnalyzer.h"

#include <string>
#include <utility>

namespace rhc {
namespace {

BuiltinType* asBuiltin(Type* type) {
    return type ? dynamic_cast<BuiltinType*>(type) : nullptr;
}

int numericRank(BuiltinType* type) {
    if (!type) return 0;
    switch (type->builtinKind) {
    case BuiltinTypeKind::Desimal: return 100;
    case BuiltinTypeKind::Bilangan64:
    case BuiltinTypeKind::BilanganTanpaTanda64: return 90;
    case BuiltinTypeKind::Bilangan: return 80;
    case BuiltinTypeKind::Bilangan32:
    case BuiltinTypeKind::BilanganTanpaTanda32: return 70;
    case BuiltinTypeKind::Bilangan16:
    case BuiltinTypeKind::BilanganTanpaTanda16: return 60;
    case BuiltinTypeKind::Bilangan8:
    case BuiltinTypeKind::BilanganTanpaTanda8: return 50;
    case BuiltinTypeKind::Karakter: return 40;
    default: return 0;
    }
}

} // namespace

Type* SemanticAnalyzer::analyzeExpression(ExprNode* expr) {
    if (!expr) return context_.types().getErrorType();
    Type* result = context_.types().getErrorType();

    switch (expr->kind) {
    case AstKind::IntegerLiteralExpr: {
        const std::string text = std::string(sourceManager_.getText(
            sourceManager_.getFileID(expr->range.begin), expr->range.begin, expr->range.length));
        const bool isFloat = text.find('.') != std::string::npos ||
                             text.find('e') != std::string::npos ||
                             text.find('E') != std::string::npos;
        result = context_.types().getBuiltinType(isFloat ? BuiltinTypeKind::Desimal
                                                         : BuiltinTypeKind::Bilangan);
        break;
    }
    case AstKind::StringLiteralExpr:
        result = context_.types().getBuiltinType(BuiltinTypeKind::Teks);
        break;
    case AstKind::CharLiteralExpr:
        result = context_.types().getBuiltinType(BuiltinTypeKind::Karakter);
        break;
    case AstKind::BoolLiteralExpr:
        result = context_.types().getBuiltinType(BuiltinTypeKind::Logika);
        break;
    case AstKind::NullLiteralExpr: {
        // Null: pointer ke expected-type (jika konteks memberi pointer),
        // selain itu pointer ke Hampa. Sebelumnya pointer ke ErrorType —
        // memproduksi ErrorType bersarang tanpa diagnostic dan merusak
        // pemakaian seperti "var p: tunjuk ke T = kosong".
        Type* nullType = expectedType();
        if (!nullType || nullType->kind() != TypeKind::Pointer) {
            nullType = context_.types().getPointerType(
                context_.types().getBuiltinType(BuiltinTypeKind::Hampa));
        }
        result = nullType;
        break;
    }
    case AstKind::IdentifierExpr:
        result = analyzeIdentifier(static_cast<IdentifierExpr*>(expr));
        break;
    case AstKind::CallExpr:
        result = analyzeCall(static_cast<CallExpr*>(expr));
        break;
    case AstKind::BinaryExpr:
        result = analyzeBinary(static_cast<BinaryExpr*>(expr));
        break;
    case AstKind::TernaryExpr:
        result = analyzeTernary(static_cast<TernaryExpr*>(expr));
        break;
    case AstKind::UnaryExpr:
        result = analyzeUnary(static_cast<UnaryExpr*>(expr));
        break;
    case AstKind::NamedArgumentExpr:
        result = analyzeExpression(static_cast<NamedArgumentExpr*>(expr)->value);
        break;
    case AstKind::ArrayLiteralExpr:
        result = analyzeArrayLiteral(static_cast<ArrayLiteralExpr*>(expr));
        break;
    case AstKind::ArrayIndexExpr:
        result = analyzeArrayIndex(static_cast<ArrayIndexExpr*>(expr));
        break;
    case AstKind::SliceExpr:
        result = analyzeSlice(static_cast<SliceExpr*>(expr));
        break;
    case AstKind::MemberExpr:
        result = analyzeMember(static_cast<MemberExpr*>(expr));
        break;
    case AstKind::TypeExpr:
        if (auto* typeExpr = static_cast<TypeExpr*>(expr)) {
            (void)resolver_.resolve(typeExpr->type);
        }
        break;
    case AstKind::ErrorExpr:
        break;
    case AstKind::LambdaExpr:
        result = analyzeLambda(static_cast<LambdaExpr*>(expr));
        break;
    default:
        error(expr->range, "unsupported expression in semantic v1");
        break;
    }

    context_.setExprType(expr, result);
    return result;
}


Type* SemanticAnalyzer::commonType(Type* lhs, Type* rhs) {
    if (!lhs || !rhs) return context_.types().getErrorType();
    if (lhs == rhs) return lhs;
    if (dynamic_cast<ErrorType*>(lhs) || dynamic_cast<ErrorType*>(rhs)) {
        return context_.types().getErrorType();
    }
    if (isNumericType(lhs) && isNumericType(rhs)) {
        return numericRank(asBuiltin(lhs)) >= numericRank(asBuiltin(rhs)) ? lhs : rhs;
    }
    return context_.types().getErrorType();
}

Type* SemanticAnalyzer::analyzeArrayLiteral(ArrayLiteralExpr* expr) {
    if (expr->elements.empty()) {
        if (Type* expected = expectedType()) {
            if (auto* arrayType = dynamic_cast<ArrayType*>(expected)) {
                return context_.types().getArrayType(arrayType->elementType, 0);
            }
        }
        error(expr->range, "empty array literal needs a target type");
        return context_.types().getErrorType();
    }

    Type* elementType = analyzeExpression(expr->elements.front());
    for (std::size_t i = 1; i < expr->elements.size(); ++i) {
        Type* nextType = analyzeExpression(expr->elements[i]);
        elementType = commonType(elementType, nextType);
    }
    if (!elementType) return context_.types().getErrorType();
    return context_.types().getArrayType(elementType, expr->elements.size());
}

static IdentifierExpr* resolveBaseIdentifier(ExprNode* node) {
    while (node) {
        if (node->kind == AstKind::IdentifierExpr) return static_cast<IdentifierExpr*>(node);
        if (node->kind == AstKind::ArrayIndexExpr) {
            node = static_cast<ArrayIndexExpr*>(node)->object;
            continue;
        }
        if (node->kind == AstKind::MemberExpr) {
            node = static_cast<MemberExpr*>(node)->object;
            continue;
        }
        break;
    }
    return nullptr;
}

static Type* opsiOf(SemanticContext& context, Type* valueType) {
    Symbol* opsiSymbol = context.globalScope().lookup("Opsi");
    auto* opsiType = opsiSymbol ? dynamic_cast<SumType*>(opsiSymbol->type) : nullptr;
    if (!opsiType) return context.types().getErrorType();
    return context.types().getGenericInstantiation(opsiType, {{"T", valueType}});
}

static Type* opsiOfIterator(SemanticContext& context, Type* elementType) {
    // Opsi(Iterator(elem)): a slice that may fail is an optional iterator.
    auto* iteratorObject = context.types().getBuiltinObjectType("Iterator");
    auto* iteratorInstantiation = context.types().getGenericInstantiation(
        iteratorObject, {{"T", elementType}});
    Symbol* opsiSymbol = context.globalScope().lookup("Opsi");
    auto* opsiType = opsiSymbol ? dynamic_cast<SumType*>(opsiSymbol->type) : nullptr;
    if (!opsiType) return context.types().getErrorType();
    return context.types().getGenericInstantiation(opsiType, {{"T", iteratorInstantiation}});
}

Type* SemanticAnalyzer::analyzeArrayIndex(ArrayIndexExpr* expr) {
    Type* objectType = analyzeExpression(expr->object);
    bool isSlice = expr->index && expr->index->kind == AstKind::SliceExpr;
    bool isMultiSlice = false;
    if (!isSlice && expr->index && expr->index->kind == AstKind::ArrayLiteralExpr) {
        for (ExprNode* element : static_cast<ArrayLiteralExpr*>(expr->index)->elements) {
            if (element && element->kind == AstKind::SliceExpr) {
                isMultiSlice = isSlice = true;
                break;
            }
        }
    }
    (void)isMultiSlice;
    Type* indexType = analyzeExpression(expr->index);

    if (!isSlice && !isNumericType(indexType) &&
        indexType != context_.types().getErrorType()) {
        error(expr->index->range, "array index must be numeric");
    }

    if (auto* arrayType = dynamic_cast<ArrayType*>(objectType)) {
        if (isSlice) return opsiOfIterator(context_, arrayType->elementType);
        return arrayType->elementType;
    }
    if (auto* pointerType = dynamic_cast<PointerType*>(objectType)) {
        return pointerType->pointee;
    }
    if (isSlice && dynamic_cast<BuiltinObjectType*>(objectType)) {
        // Slicing a builtin collection (e.g. Daftar) yields an optional iterator;
        // element type unknown for the non-generic form.
        return opsiOfIterator(context_, context_.types().getErrorType());
    }
    if (isSlice) {
        if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(objectType)) {
            if (dynamic_cast<BuiltinObjectType*>(instantiation->generic)) {
                Type* elementType = context_.types().getErrorType();
                for (const auto& [argumentName, argumentType] : instantiation->arguments) {
                    if (argumentName == "T") elementType = argumentType;
                }
                return opsiOfIterator(context_, elementType);
            }
        }
    }
    if (auto* builtin = dynamic_cast<BuiltinType*>(objectType);
        builtin && builtin->builtinKind == BuiltinTypeKind::Teks) {
        if (isSlice) {
            return opsiOfIterator(context_, context_.types().getBuiltinType(BuiltinTypeKind::Karakter));
        }
        return context_.types().getBuiltinType(BuiltinTypeKind::Karakter);
    }

    if (objectType != context_.types().getErrorType()) {
        error(expr->object->range, "indexing expects array or teks");
    }
    return context_.types().getErrorType();
}


Type* SemanticAnalyzer::analyzeSlice(SliceExpr* expr) {
    auto checkBound = [&](ExprNode* bound) {
        if (!bound) return;
        Type* boundType = analyzeExpression(bound);
        if (!isNumericType(boundType) && boundType != context_.types().getErrorType() &&
            boundType != context_.types().getBuiltinType(BuiltinTypeKind::Teks) &&
            boundType != context_.types().getBuiltinType(BuiltinTypeKind::Karakter)) {
            error(bound->range, "slice bound must be numeric or teks");
        }
    };
    checkBound(expr->start);
    checkBound(expr->end);
    checkBound(expr->step);
    return context_.types().getSliceType(context_.types().getErrorType());
}

Type* SemanticAnalyzer::analyzeMember(MemberExpr* expr) {
    Type* objectType = analyzeExpression(expr->object);
    if (objectType == context_.types().getErrorType()) {
        return context_.types().getErrorType();
    }
    Type* structTypeCandidate = objectType;

    if (expr->isArrow) {
        if (auto* pointerType = dynamic_cast<PointerType*>(objectType)) {
            structTypeCandidate = pointerType->pointee;
        }
    }
    // Arrow capture: `expr -> nama` declares `nama` with the value of expr
    // (await/system-style result annotation), not pointer member access.
    if (expr->isArrow && !dynamic_cast<StructType*>(structTypeCandidate)) {
        // `f(...) -> nama`: assigns to an existing variable, or declares a
        // new one when the name is not taken yet.
        Symbol* existing = currentScope_->lookupLocal(expr->memberName);
        if (!existing) {
            Symbol* captured = context_.createSymbol(
                SymbolKind::LocalVar, expr->memberName, structTypeCandidate, nullptr,
                expr->range, currentScope_);
            currentScope_->insert(captured);
            declareTracked(captured, true);
        } else {
            markAssigned(existing);
        }
        return structTypeCandidate;
    }

    if (auto* builtinObject = dynamic_cast<BuiltinObjectType*>(structTypeCandidate)) {
        const auto it = builtinObject->members.find(expr->memberName);
        if (it != builtinObject->members.end()) {
            return it->second;
        }
        error(expr->range, "unknown builtin member: " + expr->memberName);
        return context_.types().getErrorType();
    }

    // Enum case access: Daftar_Tipe.OPSI_BILANGAN
    if (auto* sum = dynamic_cast<SumType*>(structTypeCandidate)) {
        if (const SumType::Case* c = sum->findCase(expr->memberName)) {
            return c->aliasType ? c->aliasType : static_cast<Type*>(sum);
        }
        error(expr->range, "unknown case: " + expr->memberName);
        return context_.types().getErrorType();
    }

    // Generic struct instance: field types are substituted with the arguments.
    std::unordered_map<TypeVariableType*, Type*> substitutions;
    if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(structTypeCandidate)) {
        if (auto* structure = dynamic_cast<StructType*>(instantiation->generic)) {
            substitutions = instantiationMap(instantiation);
            structTypeCandidate = structure;
        } else if (auto* object = dynamic_cast<BuiltinObjectType*>(instantiation->generic)) {
            const auto it = object->members.find(expr->memberName);
            if (it != object->members.end()) {
                return substitute(it->second, substitutions.empty()
                    ? instantiationMap(instantiation) : substitutions);
            }
            error(expr->range, "unknown builtin member: " + expr->memberName);
            return context_.types().getErrorType();
        } else if (auto* sum = dynamic_cast<SumType*>(instantiation->generic)) {
            if (const SumType::Case* c = sum->findCase(expr->memberName)) {
                substitutions = instantiationMap(instantiation);
                return c->aliasType ? substitute(c->aliasType, substitutions)
                                    : static_cast<Type*>(structTypeCandidate);
            }
            error(expr->range, "unknown case: " + expr->memberName);
            return context_.types().getErrorType();
        }
    }

    auto* structType = dynamic_cast<StructType*>(structTypeCandidate);
    if (!structType) {
        error(expr->object->range, "member access requires struct");
        return context_.types().getErrorType();
    }

    for (Symbol* field : structType->fields) {
        if (field->name == expr->memberName) {
            Type* fieldType = field->type ? field->type : context_.types().getErrorType();
            return substitutions.empty() ? fieldType : substitute(fieldType, substitutions);
        }
    }

    error(expr->range, "unknown struct member: " + expr->memberName);
    return context_.types().getErrorType();
}

Type* SemanticAnalyzer::analyzeIdentifier(IdentifierExpr* expr) {
    Symbol* symbol = currentScope_ ? currentScope_->lookup(expr->name) : nullptr;
    if (!symbol) {
        error(expr->range, "unknown identifier: " + expr->name);
        return context_.types().getErrorType();
    }
    if (!symbol->type) {
        error(expr->range, "identifier has no type: " + expr->name);
        return context_.types().getErrorType();
    }
    if (symbol->kind == SymbolKind::LocalVar && suppressReadChecks_ == 0 &&
        !isDefinitelyAssigned(symbol)) {
        error(expr->range, "variable '" + expr->name + "' may be uninitialized");
    }
    return symbol->type;
}

Type* SemanticAnalyzer::analyzeTernary(TernaryExpr* expr) {
    Type* conditionType = analyzeExpression(expr->condition);
    if (!isLogicType(conditionType)) {
        error(expr->condition->range, "ternary condition must be logika");
    }
    Type* trueType = analyzeExpression(expr->trueValue);
    Type* falseType = analyzeExpression(expr->falseValue);
    return commonType(trueType, falseType);
}

Type* SemanticAnalyzer::analyzeCall(CallExpr* expr) {
    // A variable passed as any call argument may be written by the callee
    // (conservative escape): treat it as assigned afterwards.
    auto markEscapedArgument = [&](ExprNode* argument) {
        ExprNode* base = argument;
        while (base && base->kind == AstKind::ArrayIndexExpr) {
            base = static_cast<ArrayIndexExpr*>(base)->object;
        }
        if (base && base->kind == AstKind::IdentifierExpr) {
            Symbol* symbol = currentScope_->lookup(
                static_cast<IdentifierExpr*>(base)->name);
            if (symbol) markAssigned(symbol);
        }
    };
    // A call may write through its arguments (out-params): mark identifier
    // arguments BEFORE analysing them, so the read check stays silent.
    for (ExprNode* argument : expr->arguments) {
        markEscapedArgument(argument);
    }
    // `hasil(x)` / `hasil dari x` inside a generator or coroutine.
    if (expr->callee->kind == AstKind::IdentifierExpr) {
        const std::string& calleeName = static_cast<IdentifierExpr*>(expr->callee)->name;
        if (calleeName == "hasil") {
            bool inGenerator = currentFunctionDecl_ &&
                (std::find(currentFunctionDecl_->modifiers.begin(),
                           currentFunctionDecl_->modifiers.end(),
                           FunctionModifier::Generator) !=
                     currentFunctionDecl_->modifiers.end() ||
                 std::find(currentFunctionDecl_->modifiers.begin(),
                           currentFunctionDecl_->modifiers.end(),
                           FunctionModifier::Coroutine) !=
                     currentFunctionDecl_->modifiers.end());
            if (!inGenerator) {
                error(expr->range, "hasil outside generator or coroutine");
                for (ExprNode* argument : expr->arguments) {
                    analyzeExpression(argument);
                }
            } else if (expr->arguments.size() == 1 && currentFunctionType_) {
                Type* yieldType = nullptr;
                if (auto* instantiation =
                        dynamic_cast<GenericInstantiationType*>(currentFunctionType_->returnType)) {
                    std::vector<TypeVariableType*> params = typeParamsOf(instantiation->generic);
                    for (std::size_t i = 0; i < params.size(); ++i) {
                        if (params[i]->name != "T") continue;
                        for (const auto& [argumentName, argumentType] : instantiation->arguments) {
                            if (argumentName == "T") yieldType = argumentType;
                        }
                        if (!yieldType && i < instantiation->arguments.size()) {
                            yieldType = instantiation->arguments[i].second;
                        }
                        break;
                    }
                }
                if (yieldType) pushExpectedType(yieldType);
                Type* valueType = analyzeExpression(expr->arguments[0]);
                if (yieldType) popExpectedType();
                if (yieldType && !isAssignableTo(valueType, yieldType)) {
                    // `hasil dari sub()` yields each element of a sub-generator.
                    bool yieldsFromGenerator = false;
                    if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(valueType)) {
                        if (auto* object = dynamic_cast<BuiltinObjectType*>(instantiation->generic)) {
                            if (object->name == "Pembangkit" || object->name == "Iterator") {
                                for (const auto& [argumentName, argumentType] : instantiation->arguments) {
                                    if (argumentName == "T" && isAssignableTo(argumentType, yieldType)) {
                                        yieldsFromGenerator = true;
                                    }
                                }
                            }
                        }
                    }
                    if (!yieldsFromGenerator) {
                        error(expr->arguments[0]->range, "hasil value type mismatch");
                    }
                }
            }
            return context_.types().getBuiltinType(BuiltinTypeKind::Hampa);
        }

        // E-6: isi(ptr) dereferences a pointer; the operand must be tunjuk.
        if (calleeName == "isi" && expr->arguments.size() == 1) {
            Type* argumentType = analyzeExpression(expr->arguments[0]);
            markEscapedArgument(expr->arguments[0]);
            if (argumentType == context_.types().getErrorType()) {
                return context_.types().getErrorType();
            }
            if (auto* pointer = dynamic_cast<PointerType*>(argumentType)) {
                return pointer->pointee;
            }
            error(expr->arguments[0]->range, "isi() requires a tunjuk (pointer) operand");
            return context_.types().getErrorType();
        }

        // Constructor call: unknown identifier called with a cacahan_sum expected type.
        Symbol* calleeSymbol = currentScope_->lookup(calleeName);
        if (!calleeSymbol) {
            if (Type* constructorType = tryResolveConstructorCall(expr, calleeName)) {
                return constructorType;
            }
        }
    }

    Type* calleeType = analyzeExpression(expr->callee);
    auto* functionType = dynamic_cast<FunctionType*>(calleeType);
    if (!functionType && calleeType == context_.types().getErrorType()) {
        for (ExprNode* argument : expr->arguments) {
            analyzeExpression(argument);
        }
        return context_.types().getErrorType();
    }
    if (!functionType) {
        // Construction call on a type: desimal_tetap("1.5"), Pembangkit(dengan T = ...).
        if (dynamic_cast<BuiltinObjectType*>(calleeType) ||
            dynamic_cast<StructType*>(calleeType) ||
            dynamic_cast<SumType*>(calleeType) ||
            dynamic_cast<GenericInstantiationType*>(calleeType)) {
            for (ExprNode* argument : expr->arguments) {
                if (auto* named = dynamic_cast<NamedArgumentExpr*>(argument)) {
                    analyzeExpression(named->value);
                } else if (argument->kind == AstKind::TypeExpr) {
                    resolver_.resolve(static_cast<TypeExpr*>(argument)->type);
                } else {
                    analyzeExpression(argument);
                }
            }
            return calleeType;
        }
        error(expr->callee->range, "expression is not callable");
        for (ExprNode* argument : expr->arguments) {
            analyzeExpression(argument);
        }
        return context_.types().getErrorType();
    }

    // Generic function: collect explicit type arguments and infer the rest.
    std::unordered_map<TypeVariableType*, Type*> substitutions;
    std::vector<ExprNode*> valueArguments;
    if (!functionType->typeParams.empty()) {
        for (ExprNode* argument : expr->arguments) {
            if (auto* named = dynamic_cast<NamedArgumentExpr*>(argument)) {
                TypeVariableType* variable = nullptr;
                for (TypeVariableType* candidate : functionType->typeParams) {
                    if (candidate->name == named->name) {
                        variable = candidate;
                        break;
                    }
                }
                if (variable && named->value && named->value->kind == AstKind::TypeExpr) {
                    substitutions[variable] =
                        resolver_.resolve(static_cast<TypeExpr*>(named->value)->type);
                    continue;
                }
            }
            valueArguments.push_back(argument);
        }
        std::vector<Type*> argumentTypes;
        argumentTypes.reserve(valueArguments.size());
        std::size_t paramIndex = 0;
        for (ExprNode* argument : valueArguments) {
            Type* argumentType = analyzeExpression(argument);
            argumentTypes.push_back(argumentType);
            markEscapedArgument(argument);
            if (paramIndex < functionType->params.size()) {
                Type* paramType = substitute(functionType->params[paramIndex].type, substitutions);
                unify(paramType, argumentType, substitutions);
                ++paramIndex;
            }
        }
        if (!functionType->isVariadic &&
            valueArguments.size() != functionType->params.size()) {
            error(expr->range, "wrong number of call arguments");
        }
        for (std::size_t i = 0; i < argumentTypes.size() && i < functionType->params.size(); ++i) {
            Type* paramType = substitute(functionType->params[i].type, substitutions);
            if (!isAssignableTo(argumentTypes[i], paramType)) {
                error(valueArguments[i]->range, "argument type mismatch");
            }
        }
        if (getenv("RHEINC_DEBUG_TRAIT")) {
            fprintf(stderr, "[trait] fn constraints=%zu subs=%zu typeParams=%zu\n",
                    functionType->constraints.size(), substitutions.size(),
                    functionType->typeParams.size());
        }
        // Kategori C: verify trait constraints for inferred/explicit bindings.
        for (const auto& [variable, traits] : functionType->constraints) {
            auto it = substitutions.find(variable);
            if (it == substitutions.end()) continue;
            for (const std::string& trait : traits) {
                if (!satisfiesBuiltinTrait(it->second, trait)) {
                    error(expr->range, "type argument '" + variable->name +
                                       "' does not satisfy '" + trait + "'");
                }
            }
        }
        // Placeholder ErrorType (registerVariadic polimorfik): turunkan
        // dari expected-type context (mis. tipe deklarasi variabel) atau
        // jadikan Hampa untuk pemakaian statement. Sebelumnya ErrorType
        // dipropagasikan mentah tanpa diagnostic.
        Type* rawResult = substitute(functionType->returnType, substitutions);
        if (rawResult == context_.types().getErrorType()) {
            if (Type* expected = expectedType()) return expected;
            // Tanpa konteks expected: biarkan ErrorType (placeholder) —
            // menggantinya dengan Hampa mengekspos error tersembunyi di
            // value-context dan merusak pemakaian yang sebelumnya valid.
        }
        return rawResult;
    }

    std::vector<Type*> argumentTypes;
    argumentTypes.reserve(expr->arguments.size());
    for (ExprNode* argument : expr->arguments) {
        {
            Type* argumentType = analyzeExpression(argument);
            argumentTypes.push_back(argumentType);
            markEscapedArgument(argument);
        }
    }

    if (!functionType->isVariadic && argumentTypes.size() != functionType->params.size()) {
        error(expr->range, "wrong number of call arguments");
        return functionType->returnType;
    }

    if (!functionType->isVariadic) {
        for (std::size_t i = 0; i < argumentTypes.size(); ++i) {
            if (!isAssignableTo(argumentTypes[i], functionType->params[i].type)) {
                error(expr->arguments[i]->range, "argument type mismatch");
            }
        }
    }

    {
        // Sama untuk jalur variadic (lihat catatan di atas).
        Type* rawResult = functionType->returnType;
        if (rawResult == context_.types().getErrorType()) {
            if (Type* expected = expectedType()) return expected;
        }
        return rawResult;
    }
}

Type* SemanticAnalyzer::analyzeBinary(BinaryExpr* expr) {
    if (expr->op == TokenKind::Equal) {
        ++suppressReadChecks_;
        Type* lhsType = analyzeExpression(expr->lhs);
        --suppressReadChecks_;
        if (expr->lhs->kind != AstKind::IdentifierExpr &&
            expr->lhs->kind != AstKind::MemberExpr &&
            expr->lhs->kind != AstKind::ArrayIndexExpr &&
            expr->lhs->kind != AstKind::CallExpr) {
            error(expr->lhs->range, "assignment target must be an lvalue");
        }
        if (IdentifierExpr* base = resolveBaseIdentifier(expr->lhs)) {
            Symbol* target = currentScope_->lookup(base->name);
            if (target) {
                if (expr->lhs->kind == AstKind::IdentifierExpr) {
                    bool isConstDecl = false;
                    if (auto* global = dynamic_cast<GlobalVarDecl*>(target->decl)) {
                        isConstDecl = global->isConst;
                    } else if (auto* local = dynamic_cast<VarDeclStmt*>(target->decl)) {
                        isConstDecl = local->isConst;
                    }
                    if (!isConstDecl && constLocals_.count(target)) isConstDecl = true;
                    if (isConstDecl) {
                        error(expr->range, "cannot assign to konstanta tetap: " + base->name);
                    }
                }
                markAssigned(target);
            }
        }
        // Slice assignment: a[i sampai j] = <array of element>
        if (expr->lhs->kind == AstKind::ArrayIndexExpr) {
            auto* indexExpr = static_cast<ArrayIndexExpr*>(expr->lhs);
            if (indexExpr->index && indexExpr->index->kind == AstKind::SliceExpr) {
                Type* rhsType = analyzeExpression(expr->rhs);
                bool ok = false;
                if (auto* arrayType = dynamic_cast<ArrayType*>(rhsType)) {
                    Type* objectType = analyzeExpression(indexExpr->object);
                    if (auto* lhsArray = dynamic_cast<ArrayType*>(objectType)) {
                        ok = isAssignableTo(arrayType->elementType, lhsArray->elementType);
                    }
                }
                if (!ok && rhsType != context_.types().getErrorType()) {
                    error(expr->range, "slice assignment expects an array of element type");
                }
                return lhsType;
            }
        }
        pushExpectedType(lhsType);
        Type* rhsType = analyzeExpression(expr->rhs);
        popExpectedType();
        if (!isAssignableTo(rhsType, lhsType)) {
            error(expr->range, "type mismatch in assignment");
        }
        return lhsType;
    }

    Type* lhsType = analyzeExpression(expr->lhs);
    Type* rhsType = analyzeExpression(expr->rhs);
    Type* logika = context_.types().getBuiltinType(BuiltinTypeKind::Logika);

    switch (expr->op) {
    case TokenKind::PlusEqual:
    case TokenKind::MinusEqual:
    case TokenKind::StarEqual:
    case TokenKind::SlashEqual:
    case TokenKind::SlashSlashEqual:
    case TokenKind::PercentEqual:
    case TokenKind::StarStarEqual:
    case TokenKind::LessLessEqual:
    case TokenKind::GreaterGreaterEqual:
    case TokenKind::AmpEqual:
    case TokenKind::PipeEqual:
    case TokenKind::CaretEqual:
        if (expr->lhs->kind != AstKind::IdentifierExpr &&
            expr->lhs->kind != AstKind::MemberExpr &&
            expr->lhs->kind != AstKind::ArrayIndexExpr &&
            expr->lhs->kind != AstKind::CallExpr) {
            error(expr->lhs->range, "compound assignment target must be an lvalue");
        }
        if ((lhsType != context_.types().getErrorType() && !isNumericType(lhsType)) ||
            (rhsType != context_.types().getErrorType() && !isNumericType(rhsType))) {
            error(expr->range, "compound assignment requires numeric operands");
            return context_.types().getErrorType();
        }
        if (IdentifierExpr* base = resolveBaseIdentifier(expr->lhs)) {
            Symbol* target = currentScope_->lookup(base->name);
            if (target) markAssigned(target);
        }
        return lhsType == context_.types().getErrorType() ? rhsType : lhsType;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
        return logika;
    case TokenKind::KwDan:
    case TokenKind::KwAtau:
        if (!isLogicType(lhsType) || !isLogicType(rhsType)) {
            error(expr->range, "logical operator requires logika operands");
        }
        return logika;
    case TokenKind::At:
    case TokenKind::Amp:
    case TokenKind::Pipe:
    case TokenKind::Caret:
    case TokenKind::LessLess:
    case TokenKind::GreaterGreater:
    case TokenKind::Plus:
    case TokenKind::Minus:
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::SlashSlash:
    case TokenKind::Percent:
    case TokenKind::StarStar:
        // Operands wrapped in Opsi(T) unwrap implicitly for arithmetic.
        {
            auto unwrapOpsi = [&](Type* type) -> Type* {
                if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(type)) {
                    if (auto* sum = dynamic_cast<SumType*>(instantiation->generic);
                        sum && sum->name == "Opsi") {
                        for (const auto& [argumentName, argumentType] : instantiation->arguments) {
                            if (argumentName == "T") return argumentType;
                        }
                    }
                }
                return type;
            };
            lhsType = unwrapOpsi(lhsType);
            rhsType = unwrapOpsi(rhsType);
        }
        // Arithmetic is error-tolerant: a failed operand propagates quietly.
        if (lhsType == context_.types().getErrorType() ||
            rhsType == context_.types().getErrorType()) {
            return context_.types().getErrorType();
        }
        // Fallible division: desimal_tetap and bilangan_kompleks yield Opsi(T).
        // bilangan_tanpa_batas is a distinct type with plain division results.
        if (expr->op == TokenKind::Slash && lhsType == rhsType) {
            auto objectNameOf = [](Type* type) -> std::string {
                if (auto* object = dynamic_cast<BuiltinObjectType*>(type)) return object->name;
                if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(type)) {
                    if (auto* object = dynamic_cast<BuiltinObjectType*>(instantiation->generic)) {
                        return object->name;
                    }
                }
                return {};
            };
            const std::string objectName = objectNameOf(lhsType);
            if (objectName == "desimal_tetap" || objectName == "bilangan_kompleks") {
                return opsiOf(context_, lhsType);
            }
        }
        {
            Type* logikaType = context_.types().getBuiltinType(BuiltinTypeKind::Logika);
            if ((!isNumericType(lhsType) && lhsType != logikaType) ||
                (!isNumericType(rhsType) && rhsType != logikaType)) {
                error(expr->range, "arithmetic operator requires numeric operands");
                return context_.types().getErrorType();
            }
        }
        if (lhsType == rhsType) return lhsType;
        if (numericRank(asBuiltin(lhsType)) >= numericRank(asBuiltin(rhsType))) return lhsType;
        return rhsType;
    case TokenKind::PlusGreater: {
        // Append buffer teks (a +> b): hasil bertipe teks.
        Type* teksType = context_.types().getBuiltinType(BuiltinTypeKind::Teks);
        if (lhsType == teksType && rhsType == teksType) return teksType;
        error(expr->range, "append (+>) requires teks operands");
        return context_.types().getErrorType();
    }
    default:
        error(expr->range, "unsupported binary operator in semantic v1");
        return context_.types().getErrorType();
    }
}

Type* SemanticAnalyzer::analyzeUnary(UnaryExpr* expr) {
    Type* operandType = analyzeExpression(expr->operand);

    if (expr->op == UnaryOp::Await) {
        // The `-> nama` annotation is handled by the arrow-capture member
        // access, so the operand keeps its type (e.g. Opsi(T) for pilih).
        return operandType;
    }

    switch (expr->op) {
    case UnaryOp::Increment:
    case UnaryOp::Decrement:
        if (expr->operand->kind != AstKind::IdentifierExpr &&
            expr->operand->kind != AstKind::MemberExpr &&
            expr->operand->kind != AstKind::ArrayIndexExpr) {
            error(expr->operand->range, "increment/decrement requires lvalue operand");
        }
        if (!isNumericType(operandType) &&
            operandType != context_.types().getBuiltinType(BuiltinTypeKind::Logika)) {
            error(expr->range, "increment/decrement requires numeric operand");
            return context_.types().getErrorType();
        }
        if (expr->operand->kind == AstKind::IdentifierExpr) {
            Symbol* target = currentScope_->lookup(
                static_cast<IdentifierExpr*>(expr->operand)->name);
            if (target) markAssigned(target);
        }
        return operandType;
    case UnaryOp::BitNot:
    case UnaryOp::Minus:
    case UnaryOp::Plus:
        if (!isNumericType(operandType)) {
            error(expr->range, "unary +/- requires numeric operand");
            return context_.types().getErrorType();
        }
        return operandType;
    case UnaryOp::LogicalNot:
        if (operandType == context_.types().getErrorType()) {
            return context_.types().getErrorType();
        }
        if (!isLogicType(operandType)) {
            error(expr->range, "bukan requires logika operand");
            return context_.types().getErrorType();
        }
        return context_.types().getBuiltinType(BuiltinTypeKind::Logika);
    case UnaryOp::AddressOf:
        if (expr->operand->kind != AstKind::IdentifierExpr &&
            expr->operand->kind != AstKind::MemberExpr &&
            expr->operand->kind != AstKind::ArrayIndexExpr &&
            expr->operand->kind != AstKind::CallExpr) {
            error(expr->operand->range, "alamat requires lvalue operand");
        }
        return context_.types().getPointerType(operandType);
    case UnaryOp::Await:
        break;
    }

    error(expr->range, "unsupported unary operator in semantic v1");
    return context_.types().getErrorType();
}

Type* SemanticAnalyzer::analyzeLambda(LambdaExpr* expr) {
    Scope* previousScope = currentScope_;
    FunctionType* previousFunctionType = currentFunctionType_;
    currentScope_ = currentScope_->createChild();

    resolver_.pushTypeVariables(expr->typeParameters);
    std::vector<TypeVariableType*> typeVariables = resolver_.currentTypeVariables();

    std::vector<FunctionParamType> params;
    bool parameterError = false;
    for (const auto& [paramName, paramTypeRepr] : expr->parameters) {
        Type* paramType = nullptr;
        if (paramTypeRepr) {
            paramType = resolver_.resolve(paramTypeRepr);
        } else {
            error(expr->range, "lambda parameter requires a type");
            paramType = context_.types().getErrorType();
            parameterError = true;
        }
        params.push_back(FunctionParamType{paramName, paramType});
        Symbol* paramSymbol = context_.createSymbol(
            SymbolKind::Param, paramName, paramType, nullptr, expr->range, currentScope_);
        currentScope_->insert(paramSymbol);
    }

    Type* declaredReturn = expr->returnType ? resolver_.resolve(expr->returnType) : nullptr;
    Type* errorType = context_.types().getErrorType();
    Type* hampa = context_.types().getBuiltinType(BuiltinTypeKind::Hampa);

    FunctionType* functionType = nullptr;
    if (declaredReturn) {
        functionType = context_.types().getFunctionType(params, declaredReturn, false, typeVariables);
        currentFunctionType_ = functionType;
        analyzeStatement(expr->body);
        checkReturnPaths(expr->body, declaredReturn, expr->range);
    } else {
        if (parameterError) {
            functionType = context_.types().getFunctionType(params, errorType, false, typeVariables);
            currentFunctionType_ = functionType;
            analyzeStatement(expr->body);
        } else {
            // Return type inference: analyze the body while collecting return types.
            auto* placeholder = context_.types().getFunctionType(params, errorType, false, typeVariables);
            currentFunctionType_ = placeholder;
            returnCollectors_.emplace_back();
            analyzeStatement(expr->body);
            std::vector<Type*> returns = std::move(returnCollectors_.back());
            returnCollectors_.pop_back();

            Type* inferredReturn = nullptr;
            for (Type* returnType : returns) {
                if (returnType == hampa) continue;
                inferredReturn = inferredReturn ? commonType(inferredReturn, returnType) : returnType;
            }
            if (!inferredReturn) inferredReturn = hampa;
            functionType = context_.types().getFunctionType(std::move(params), inferredReturn,
                                                            false, typeVariables);
        }
    }

    if (!expr->constraints.empty()) {
        for (const auto& [tvName, traits] : expr->constraints) {
            for (auto* v : typeVariables) {
                if (v->name == tvName) {
                    functionType->constraints.emplace_back(v, traits);
                    break;
                }
            }
        }
    }
    resolver_.popTypeVariables();
    currentFunctionType_ = previousFunctionType;
    currentScope_ = previousScope;
    return functionType;
}

} // namespace rhc
