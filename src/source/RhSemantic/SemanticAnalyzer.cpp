#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/Type.h"

#include <string>
#include <utility>

namespace rhc {

SemanticAnalyzer::SemanticAnalyzer(SourceManager& sourceManager,
                                   DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics),
      resolver_(context_, diagnostics_) {
    builtins_.registerAll(context_);
}

bool SemanticAnalyzer::analyze(TranslationUnitDecl* unit) {
    if (!unit) return false;
    currentScope_ = &context_.globalScope();

    for (DeclNode* decl : unit->declarations) {
        collectDeclaration(decl);
    }
    for (DeclNode* decl : unit->declarations) {
        analyzeDeclaration(decl);
    }

    currentScope_ = &context_.globalScope();
    return !diagnostics_.hasErrors();
}

void SemanticAnalyzer::error(SourceRange range, const std::string& message) {
    diagnostics_.error(sourceManager_.getFileID(range.begin), range.begin, range.length, message);
}

bool SemanticAnalyzer::isLogicType(Type* type) const {
    if (!type) return false;
    const auto* builtin = dynamic_cast<const BuiltinType*>(type);
    return builtin && builtin->builtinKind == BuiltinTypeKind::Logika;
}

bool SemanticAnalyzer::isNumericType(Type* type) const {
    if (!type) return false;
    if (const auto* variable = dynamic_cast<const TypeVariableType*>(type)) {
        (void)variable;
        return true; // generic numeric: accepted optimistically
    }
    if (const auto* sum = dynamic_cast<const SumType*>(type)) {
        return sum->isPlainEnum;
    }
    if (const auto* instantiation = dynamic_cast<const GenericInstantiationType*>(type)) {
        // Generic numeric builtin objects, e.g. bilangan_kompleks(basis = desimal).
        const auto* object = dynamic_cast<const BuiltinObjectType*>(instantiation->generic);
        if (!object) return false;
        const bool numericObject =
            object->name == "bilangan_tanpa_batas" || object->name == "desimal_tetap" ||
            object->name == "bilangan_kompleks" || object->name == "bilangan_aksara" ||
            object->name == "matriks" || object->name == "desimal_tetap_2" ||
            object->name == "bilangan_pecahan" || object->name == "bilangan_biner";
        return numericObject;
    }
    if (const auto* object = dynamic_cast<const BuiltinObjectType*>(type)) {
        return object->name == "desimal_tetap" ||
               object->name == "bilangan_tanpa_batas" ||
               object->name == "bilangan_kompleks" ||
               object->name == "matriks";
    }
    const auto* builtin = dynamic_cast<const BuiltinType*>(type);
    if (!builtin) return false;
    switch (builtin->builtinKind) {
    case BuiltinTypeKind::Bilangan:
    case BuiltinTypeKind::Bilangan8:
    case BuiltinTypeKind::Bilangan16:
    case BuiltinTypeKind::Bilangan32:
    case BuiltinTypeKind::Bilangan64:
    case BuiltinTypeKind::BilanganTanpaTanda8:
    case BuiltinTypeKind::BilanganTanpaTanda16:
    case BuiltinTypeKind::BilanganTanpaTanda32:
    case BuiltinTypeKind::BilanganTanpaTanda64:
    case BuiltinTypeKind::Desimal:
    case BuiltinTypeKind::Karakter:
        return true;
    default:
        return false;
    }
}

bool SemanticAnalyzer::isAssignableTo(Type* source, Type* destination) const {
    if (!source || !destination) return false;
    if (source == destination) return true;
    if (dynamic_cast<ErrorType*>(source) || dynamic_cast<ErrorType*>(destination)) return true;
    // Unsubstituted type variables are optimistically compatible with anything.
    if (dynamic_cast<TypeVariableType*>(source) || dynamic_cast<TypeVariableType*>(destination)) {
        return true;
    }

    if (isNumericType(source) && isNumericType(destination)) {
        return true;
    }

    // C-style coercion: any numeric scalar converts to/from logika.
    const auto* sourceScalar = dynamic_cast<const BuiltinType*>(source);
    const auto* destinationScalar = dynamic_cast<const BuiltinType*>(destination);
    if (sourceScalar && destinationScalar) {
        const auto sk = sourceScalar->builtinKind;
        const auto dk = destinationScalar->builtinKind;
        const bool sourceNumeric = sk == BuiltinTypeKind::Bilangan ||
                                   sk == BuiltinTypeKind::Desimal ||
                                   sk == BuiltinTypeKind::Karakter;
        const bool destinationNumeric = dk == BuiltinTypeKind::Bilangan ||
                                        dk == BuiltinTypeKind::Desimal ||
                                        dk == BuiltinTypeKind::Karakter;
        if ((sourceNumeric && dk == BuiltinTypeKind::Logika) ||
            (sk == BuiltinTypeKind::Logika && destinationNumeric)) {
            return true;
        }
        // teks is freely convertible with the numeric scalars (casting.rhc).
        const bool eitherTeks = sk == BuiltinTypeKind::Teks || dk == BuiltinTypeKind::Teks;
        if (eitherTeks && (sourceNumeric || destinationNumeric)) {
            return true;
        }
    }

    if (dynamic_cast<const PointerType*>(source) && dynamic_cast<const PointerType*>(destination)) {
        return true;
    }

    auto* sourceArray = dynamic_cast<const ArrayType*>(source);
    auto* destinationArray = dynamic_cast<const ArrayType*>(destination);
    if (sourceArray && destinationArray) {
        const bool sizeCompatible = !sourceArray->size || !destinationArray->size ||
                                    *sourceArray->size == *destinationArray->size;
        return sizeCompatible && isAssignableTo(sourceArray->elementType,
                                                destinationArray->elementType);
    }

    // Object builtin: compare struktural via nama (instance TypeContext
    // berbeda per representasi sumber).
    {
        const auto* sourceObj = dynamic_cast<const BuiltinObjectType*>(source);
        const auto* destObj = dynamic_cast<const BuiltinObjectType*>(destination);
        if (sourceObj && destObj) {
            return sourceObj->name == destObj->name;
        }
    }

    // Instantiasi generik: compare struktural (generic + argumen
    // pairwise). Pointer-compare saja gagal karena TypeContext membuat
    // instance baru per representasi sumber.
    const auto* sourceInst = dynamic_cast<const GenericInstantiationType*>(source);
    const auto* destInst = dynamic_cast<const GenericInstantiationType*>(destination);
    if (sourceInst && destInst) {
        if (!sourceInst->generic || !destInst->generic) return false;
        if (sourceInst->generic != destInst->generic) {
            auto genericName = [](const Type* g) -> std::string {
                if (const auto* st = dynamic_cast<const StructType*>(g))
                    return st->decl ? st->decl->name : std::string();
                if (const auto* su = dynamic_cast<const SumType*>(g))
                    return su->name;
                if (const auto* ob = dynamic_cast<const BuiltinObjectType*>(g))
                    return ob->name;
                return {};
            };
            if (genericName(sourceInst->generic) != genericName(destInst->generic) ||
                genericName(sourceInst->generic).empty()) {
                return false;
            }
        }
        // Kelonggaran generik: instantiation tanpa argumen (konstruktor
        // seperti KOSONG()) dianggap compatible dengan instantiation
        // ber-argumen konkret — sejalan dengan aturan TypeVariable di atas.
        const std::size_t common =
            sourceInst->arguments.size() < destInst->arguments.size()
                ? sourceInst->arguments.size()
                : destInst->arguments.size();
        for (std::size_t i = 0; i < common; ++i) {
            if (!isAssignableTo(sourceInst->arguments[i].second,
                                destInst->arguments[i].second)) {
                return false;
            }
        }
        return true;  // argumen tambahan di salah sisi diabaikan (generik)
    }

    // Sum mentah vs instantiasi sum-nya (mis. KOSONG() : Opsi vs
    // deklarasi -> Opsi(dengan T = bilangan)): compatible bila nama &
    // jumlah varian sama.
    {
        const SumType* rawSum = dynamic_cast<const SumType*>(source);
        const GenericInstantiationType* inst =
            dynamic_cast<const GenericInstantiationType*>(destination);
        if (rawSum && inst && inst->generic) {
            const SumType* instSum = dynamic_cast<const SumType*>(inst->generic);
            if (instSum && rawSum->name == instSum->name &&
                rawSum->cases.size() == instSum->cases.size()) {
                return true;
            }
        }
    }

    // Plain enum values are freely interchangeable with numbers.
    const auto* sourceSum = dynamic_cast<const SumType*>(source);
    const auto* destinationSum = dynamic_cast<const SumType*>(destination);
    if (sourceSum && sourceSum->isPlainEnum && isNumericType(destination)) return true;
    if (destinationSum && destinationSum->isPlainEnum && isNumericType(source)) return true;
    if (sourceSum && destinationSum) return sourceSum == destinationSum;

    // Generic instantiations of the same template with compatible arguments.
    const auto* sourceInstantiation = dynamic_cast<const GenericInstantiationType*>(source);
    const auto* destinationInstantiation = dynamic_cast<const GenericInstantiationType*>(destination);
    if (sourceInstantiation && destinationInstantiation) {
        // Every destination argument must be satisfied by a same-named source
        // argument; extra named arguments are ignored. Mismatched sizes are OK.
        if (sourceInstantiation->generic != destinationInstantiation->generic) {
            if (sourceInstantiation->arguments.size() != destinationInstantiation->arguments.size()) {
                return false;
            }
        }
        if (sourceInstantiation->generic != destinationInstantiation->generic) {
            const auto* sourceSum = dynamic_cast<const SumType*>(sourceInstantiation->generic);
            const auto* destinationSum = dynamic_cast<const SumType*>(destinationInstantiation->generic);
            const auto* sourceObject = dynamic_cast<const BuiltinObjectType*>(sourceInstantiation->generic);
            const auto* destinationObject = dynamic_cast<const BuiltinObjectType*>(destinationInstantiation->generic);
            bool sameName =
                (sourceSum && destinationSum && sourceSum->name == destinationSum->name) ||
                (sourceObject && destinationObject && sourceObject->name == destinationObject->name);
            if (!sameName) return false;
        }
        std::size_t cursor = 0;
        for (const auto& [destinationName, destinationType] : destinationInstantiation->arguments) {
            bool satisfied = false;
            if (!destinationName.empty()) {
                for (const auto& [sourceName, sourceType] : sourceInstantiation->arguments) {
                    if (sourceName == destinationName) {
                        satisfied = isAssignableTo(sourceType, destinationType);
                        break;
                    }
                }
            }
            if (!satisfied) {
                while (cursor < sourceInstantiation->arguments.size()) {
                    satisfied = isAssignableTo(
                        sourceInstantiation->arguments[cursor].second, destinationType);
                    ++cursor;
                    break;
                }
            }
            if (!satisfied) return false;
        }
        return true;
    }
    if (sourceInstantiation || destinationInstantiation) {
        // A plain builtin object and its generic instantiation are interchangeable,
        // e.g. Daftar vs Daftar(dengan T = bilangan).
        if (auto* sourceObject = dynamic_cast<const BuiltinObjectType*>(source)) {
            if (destinationInstantiation &&
                destinationInstantiation->generic == sourceObject) return true;
        }
        if (auto* destinationObject = dynamic_cast<const BuiltinObjectType*>(destination)) {
            if (sourceInstantiation && sourceInstantiation->generic == destinationObject) {
                return true;
            }
        }
        return false;
    }

    // Function types are compatible when signatures are compatible.
    const auto* sourceFunction = dynamic_cast<const FunctionType*>(source);
    const auto* destinationFunction = dynamic_cast<const FunctionType*>(destination);
    if (sourceFunction && destinationFunction) {
        if (sourceFunction->params.size() != destinationFunction->params.size()) return false;
        for (std::size_t i = 0; i < sourceFunction->params.size(); ++i) {
            const Type* a = sourceFunction->params[i].type;
            const Type* b = destinationFunction->params[i].type;
            if (a != b && !isAssignableTo(const_cast<Type*>(a), const_cast<Type*>(b)) &&
                !isAssignableTo(const_cast<Type*>(b), const_cast<Type*>(a))) {
                return false;
            }
        }
        return isAssignableTo(sourceFunction->returnType, destinationFunction->returnType);
    }

    return false;
}



void SemanticAnalyzer::insertGlobalAllowingBuiltinShadow(Symbol* symbol) {
    Symbol* existing = context_.globalScope().lookupLocal(symbol->name);
    if (existing && isBuiltinSymbol(*existing)) {
        context_.globalScope().insertOrReplace(symbol);
        return;
    }
    if (!context_.globalScope().insert(symbol)) {
        error(symbol->range, "redefinition of name: " + symbol->name);
    }
}

std::vector<TypeVariableType*> SemanticAnalyzer::typeParamsOf(Type* type) {
    if (auto* structure = dynamic_cast<StructType*>(type)) return structure->typeParams;
    if (auto* sum = dynamic_cast<SumType*>(type)) return sum->typeParams;
    if (auto* object = dynamic_cast<BuiltinObjectType*>(type)) return object->typeParams;
    if (auto* function = dynamic_cast<FunctionType*>(type)) return function->typeParams;
    return {};
}

std::unordered_map<TypeVariableType*, Type*>
SemanticAnalyzer::instantiationMap(GenericInstantiationType* instantiation) {
    std::unordered_map<TypeVariableType*, Type*> result;
    std::vector<TypeVariableType*> params = typeParamsOf(instantiation->generic);
    for (std::size_t i = 0; i < params.size(); ++i) {
        bool found = false;
        for (const auto& [argumentName, argumentType] : instantiation->arguments) {
            if (!argumentName.empty() && argumentName == params[i]->name) {
                result[params[i]] = argumentType;
                found = true;
                break;
            }
        }
        if (!found && i < instantiation->arguments.size()) {
            result[params[i]] = instantiation->arguments[i].second;
        }
    }
    return result;
}

Type* SemanticAnalyzer::substitute(
    Type* type, const std::unordered_map<TypeVariableType*, Type*>& substitutions) {
    if (!type) return nullptr;
    if (auto* variable = dynamic_cast<TypeVariableType*>(type)) {
        auto it = substitutions.find(variable);
        return it == substitutions.end() ? type : it->second;
    }
    if (substitutions.empty()) return type;
    if (auto* pointer = dynamic_cast<PointerType*>(type)) {
        Type* pointee = substitute(pointer->pointee, substitutions);
        return pointee == pointer->pointee ? type : context_.types().getPointerType(pointee);
    }
    if (auto* array = dynamic_cast<ArrayType*>(type)) {
        Type* element = substitute(array->elementType, substitutions);
        return element == array->elementType
                   ? type
                   : context_.types().getArrayType(element, array->size);
    }
    if (auto* slice = dynamic_cast<SliceType*>(type)) {
        Type* element = substitute(slice->elementType, substitutions);
        return element == slice->elementType ? type : context_.types().getSliceType(element);
    }
    if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(type)) {
        std::vector<std::pair<std::string, Type*>> arguments;
        arguments.reserve(instantiation->arguments.size());
        for (const auto& [name, argumentType] : instantiation->arguments) {
            arguments.emplace_back(name, substitute(argumentType, substitutions));
        }
        return context_.types().getGenericInstantiation(instantiation->generic,
                                                        std::move(arguments));
    }
    if (auto* function = dynamic_cast<FunctionType*>(type)) {
        std::vector<FunctionParamType> params;
        params.reserve(function->params.size());
        for (const FunctionParamType& param : function->params) {
            params.push_back(FunctionParamType{param.name, substitute(param.type, substitutions)});
        }
        Type* returnType = substitute(function->returnType, substitutions);
        return context_.types().getFunctionType(std::move(params), returnType,
                                                function->isVariadic, function->typeParams);
    }
    return type;
}

bool SemanticAnalyzer::unify(
    Type* pattern, Type* actual,
    std::unordered_map<TypeVariableType*, Type*>& substitutions) {
    if (pattern == actual) return true;
    if (!pattern || !actual) return false;
    if (auto* variable = dynamic_cast<TypeVariableType*>(pattern)) {
        auto it = substitutions.find(variable);
        if (it == substitutions.end()) {
            substitutions[variable] = actual;
            return true;
        }
        return unify(it->second, actual, substitutions);
    }
    if (dynamic_cast<TypeVariableType*>(actual)) return unify(actual, pattern, substitutions);
    if (dynamic_cast<ErrorType*>(pattern) || dynamic_cast<ErrorType*>(actual)) return true;

    if (auto* patternBuiltin = dynamic_cast<BuiltinType*>(pattern)) {
        auto* actualBuiltin = dynamic_cast<BuiltinType*>(actual);
        return actualBuiltin && actualBuiltin->builtinKind == patternBuiltin->builtinKind;
    }
    if (auto* patternArray = dynamic_cast<ArrayType*>(pattern)) {
        auto* actualArray = dynamic_cast<ArrayType*>(actual);
        return actualArray &&
               unify(patternArray->elementType, actualArray->elementType, substitutions);
    }
    if (auto* patternPointer = dynamic_cast<PointerType*>(pattern)) {
        auto* actualPointer = dynamic_cast<PointerType*>(actual);
        return actualPointer && unify(patternPointer->pointee, actualPointer->pointee, substitutions);
    }
    if (auto* patternSlice = dynamic_cast<SliceType*>(pattern)) {
        auto* actualSlice = dynamic_cast<SliceType*>(actual);
        return actualSlice && unify(patternSlice->elementType, actualSlice->elementType, substitutions);
    }
    if (auto* patternInstantiation = dynamic_cast<GenericInstantiationType*>(pattern)) {
        // Generic instantiation against the plain (uninstantiated) object is fine.
        if (auto* actualObject = dynamic_cast<BuiltinObjectType*>(actual)) {
            if (patternInstantiation->generic == actualObject) return true;
        }
        auto* actualInstantiation = dynamic_cast<GenericInstantiationType*>(actual);
        if (!actualInstantiation ||
            patternInstantiation->arguments.size() != actualInstantiation->arguments.size()) {
            return false;
        }
        const bool sameGeneric = patternInstantiation->generic == actualInstantiation->generic;
        if (!sameGeneric) {
            // Same-name generic templates are interchangeable (e.g. user-defined
            // Opsi shadowing the built-in one).
            auto* patternSum = dynamic_cast<SumType*>(patternInstantiation->generic);
            auto* actualSum = dynamic_cast<SumType*>(actualInstantiation->generic);
            auto* patternObject = dynamic_cast<BuiltinObjectType*>(patternInstantiation->generic);
            auto* actualObject = dynamic_cast<BuiltinObjectType*>(actualInstantiation->generic);
            bool sameName =
                (patternSum && actualSum && patternSum->name == actualSum->name) ||
                (patternObject && actualObject && patternObject->name == actualObject->name);
            if (!sameName) return false;
        }
        // Match arguments by name first; unnamed leftovers pair positionally.
        // Extra named arguments (e.g. jenis = tunggal) are ignored.
        std::size_t cursor = 0;
        for (const auto& [patternName, patternType] : patternInstantiation->arguments) {
            bool done = false;
            if (!patternName.empty()) {
                for (const auto& [actualName, actualType] : actualInstantiation->arguments) {
                    if (actualName == patternName) {
                        if (!unify(patternType, actualType, substitutions)) return false;
                        done = true;
                        break;
                    }
                }
            }
            if (!done) {
                while (cursor < actualInstantiation->arguments.size()) {
                    if (!unify(patternType, actualInstantiation->arguments[cursor].second,
                               substitutions)) {
                        return false;
                    }
                    ++cursor;
                    break;
                }
            }
        }
        return true;
    }
    if (auto* patternStruct = dynamic_cast<StructType*>(pattern)) {
        auto* actualStruct = dynamic_cast<StructType*>(actual);
        return actualStruct && actualStruct->decl == patternStruct->decl;
    }
    if (auto* patternSum = dynamic_cast<SumType*>(pattern)) {
        return dynamic_cast<SumType*>(actual) == patternSum;
    }
    if (auto* patternObject = dynamic_cast<BuiltinObjectType*>(pattern)) {
        auto* actualObject = dynamic_cast<BuiltinObjectType*>(actual);
        return actualObject && actualObject->name == patternObject->name;
    }
    return false;
}

Type* SemanticAnalyzer::typeFromExpression(ExprNode* expr) {
    if (!expr) return nullptr;
    if (auto* typeExpr = dynamic_cast<TypeExpr*>(expr)) {
        return resolver_.resolve(typeExpr->type);
    }
    if (auto* identifier = dynamic_cast<IdentifierExpr*>(expr)) {
        Symbol* symbol = context_.globalScope().lookup(identifier->name);
        if (!symbol || !symbol->type) return nullptr;
        if (dynamic_cast<BuiltinType*>(symbol->type) ||
            dynamic_cast<StructType*>(symbol->type) ||
            dynamic_cast<SumType*>(symbol->type) ||
            dynamic_cast<BuiltinObjectType*>(symbol->type) ||
            dynamic_cast<GenericInstantiationType*>(symbol->type) ||
            dynamic_cast<TypeVariableType*>(symbol->type)) {
            return symbol->type;
        }
        return nullptr;
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        // cacahan_sum(X) / cacahan(X) wrapper: the type is the single argument.
        if (call->callee->kind == AstKind::IdentifierExpr) {
            const std::string& calleeName = static_cast<IdentifierExpr*>(call->callee)->name;
            if ((calleeName == "cacahan_sum" || calleeName == "cacahan") &&
                call->arguments.size() == 1) {
                return typeFromExpression(call->arguments[0]);
            }
        }
        Type* base = typeFromExpression(call->callee);
        if (!base) return nullptr;
        std::vector<TypeVariableType*> params = typeParamsOf(base);
        if (params.empty()) return base; // construction call on a concrete type
        std::vector<std::pair<std::string, Type*>> arguments;
        for (ExprNode* argument : call->arguments) {
            if (auto* named = dynamic_cast<NamedArgumentExpr*>(argument)) {
                Type* argumentType = typeFromExpression(named->value);
                if (!argumentType) return nullptr;
                arguments.emplace_back(named->name, argumentType);
                continue;
            }
            Type* argumentType = typeFromExpression(argument);
            if (!argumentType) return nullptr;
            arguments.emplace_back("", argumentType);
        }
        return context_.types().getGenericInstantiation(base, std::move(arguments));
    }
    return nullptr;
}

Type* SemanticAnalyzer::tryResolveConstructorCall(CallExpr* expr, const std::string& name) {
    Type* expected = expectedType();
    if (!expected || expected == context_.types().getErrorType()) return nullptr;

    SumType* sum = nullptr;
    std::unordered_map<TypeVariableType*, Type*> substitutions;
    if (auto* direct = dynamic_cast<SumType*>(expected)) {
        sum = direct;
    } else if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(expected)) {
        sum = dynamic_cast<SumType*>(instantiation->generic);
        if (sum) substitutions = instantiationMap(instantiation);
    }
    if (!sum) return nullptr;

    const SumType::Case* foundCase = sum->findCase(name);
    if (!foundCase) return nullptr;

    std::size_t positionalIndex = 0;
    for (ExprNode* argument : expr->arguments) {
        if (auto* named = dynamic_cast<NamedArgumentExpr*>(argument)) {
            const SumType::Field* field = nullptr;
            for (const SumType::Field& candidate : foundCase->fields) {
                if (candidate.name == named->name) {
                    field = &candidate;
                    break;
                }
            }
            if (!field) {
                error(argument->range, "unknown field: " + named->name);
                continue;
            }
            Type* fieldType = substitute(field->type, substitutions);
            pushExpectedType(fieldType);
            Type* argumentType = analyzeExpression(named->value);
            popExpectedType();
            if (!isAssignableTo(argumentType, fieldType) &&
                argumentType != context_.types().getErrorType() &&
                fieldType != context_.types().getErrorType()) {
                error(argument->range, "argument type mismatch in constructor call");
            }
            continue;
        }
        if (positionalIndex >= foundCase->fields.size()) {
            error(argument->range, "too many arguments in constructor call");
            analyzeExpression(argument);
            continue;
        }
        Type* fieldType = substitute(foundCase->fields[positionalIndex].type, substitutions);
        pushExpectedType(fieldType);
        Type* argumentType = analyzeExpression(argument);
        popExpectedType();
        if (!isAssignableTo(argumentType, fieldType)) {
            error(argument->range, "argument type mismatch in constructor call");
        }
        ++positionalIndex;
    }
    return expected;
}

bool SemanticAnalyzer::isDefinitelyAssigned(Symbol* symbol) const {
    auto it = assigned_.find(symbol);
    if (it == assigned_.end()) return true; // params, globals, captures: not tracked
    return it->second;
}

bool SemanticAnalyzer::statementTerminates(StmtNode* stmt) const {
    if (!stmt) return false;
    switch (stmt->kind) {
        case AstKind::ReturnStmt:
        case AstKind::KeluarStmt:
            return true;
        case AstKind::CompoundStmt: {
            auto* compound = static_cast<CompoundStmt*>(stmt);
            for (StmtNode* s : compound->statements) {
                if (statementTerminates(s)) return true;
            }
            return false;
        }
        case AstKind::IfStmt: {
            auto* jika = static_cast<IfStmt*>(stmt);
            if (!jika->elseBranch) return false; // may fall through when condition false
            return statementTerminates(jika->thenBranch) &&
                   statementTerminates(jika->elseBranch);
        }
        case AstKind::PilihStmt: {
            auto* pilih = static_cast<PilihStmt*>(stmt);
            if (pilih->clauses.empty()) return false;
            for (CaseClause* clause : pilih->clauses) {
                if (!statementTerminates(clause->body)) return false;
            }
            return true;
        }
        case AstKind::DoWhileStmt:
            // ulangi { } selama cond: the body always executes at least once.
            return statementTerminates(static_cast<DoWhileStmt*>(stmt)->body);
        default:
            return false;
    }
}

void SemanticAnalyzer::checkReturnPaths(StmtNode* body, Type* returnType, SourceRange range) {
    if (!returnType || returnType == context_.types().getBuiltinType(BuiltinTypeKind::Hampa)) {
        return;
    }
    if (!statementTerminates(body)) {
        error(range, "not all code paths return a value (kembalikan)");
    }
}

} // namespace rhc
