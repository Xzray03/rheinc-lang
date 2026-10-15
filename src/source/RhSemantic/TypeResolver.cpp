#include "RhSemantic/TypeResolver.h"

#include "RhParser/TypeRepr.h"

#include <string>

namespace rhc {

TypeResolver::TypeResolver(SemanticContext& context, DiagnosticEngine& diagnostics)
    : context_(context), diagnostics_(diagnostics) {}

void TypeResolver::pushTypeVariables(const std::vector<std::string>& names) {
    std::vector<TypeVariableType*> scope;
    scope.reserve(names.size());
    for (const std::string& name : names) {
        scope.push_back(context_.types().getTypeVariable(name));
    }
    typeVarScopes_.push_back(std::move(scope));
}

void TypeResolver::popTypeVariables() {
    typeVarScopes_.pop_back();
}

std::vector<TypeVariableType*> TypeResolver::currentTypeVariables() const {
    if (typeVarScopes_.empty()) return {};
    return typeVarScopes_.back();
}

TypeVariableType* TypeResolver::lookupTypeVariable(const std::string& name) const {
    for (auto it = typeVarScopes_.rbegin(); it != typeVarScopes_.rend(); ++it) {
        for (TypeVariableType* variable : *it) {
            if (variable->name == name) return variable;
        }
    }
    return nullptr;
}

void TypeResolver::error(SourceRange range, const std::string& message) {
    diagnostics_.error(FileID(), range.begin, range.length, message);
}

Type* TypeResolver::resolve(TypeReprNode* repr) {
    if (!repr) return context_.types().getErrorType();
    if (Type* cached = context_.getResolvedType(repr)) return cached;

    Type* result = context_.types().getErrorType();
    switch (repr->kind) {
    case AstKind::BuiltinTypeRepr: {
        const auto kind = static_cast<BuiltinTypeRepr*>(repr)->typeKind;
        switch (kind) {
        case TokenKind::KwBilangan: result = context_.types().getBuiltinType(BuiltinTypeKind::Bilangan); break;
        case TokenKind::KwBilangan8: result = context_.types().getBuiltinType(BuiltinTypeKind::Bilangan8); break;
        case TokenKind::KwBilangan16: result = context_.types().getBuiltinType(BuiltinTypeKind::Bilangan16); break;
        case TokenKind::KwBilangan32: result = context_.types().getBuiltinType(BuiltinTypeKind::Bilangan32); break;
        case TokenKind::KwBilangan64: result = context_.types().getBuiltinType(BuiltinTypeKind::Bilangan64); break;
        case TokenKind::KwBilanganTanpaTanda8: result = context_.types().getBuiltinType(BuiltinTypeKind::BilanganTanpaTanda8); break;
        case TokenKind::KwBilanganTanpaTanda16: result = context_.types().getBuiltinType(BuiltinTypeKind::BilanganTanpaTanda16); break;
        case TokenKind::KwBilanganTanpaTanda32: result = context_.types().getBuiltinType(BuiltinTypeKind::BilanganTanpaTanda32); break;
        case TokenKind::KwBilanganTanpaTanda64: result = context_.types().getBuiltinType(BuiltinTypeKind::BilanganTanpaTanda64); break;
        case TokenKind::KwDesimal: result = context_.types().getBuiltinType(BuiltinTypeKind::Desimal); break;
        case TokenKind::KwLogika: result = context_.types().getBuiltinType(BuiltinTypeKind::Logika); break;
        case TokenKind::KwKarakter: result = context_.types().getBuiltinType(BuiltinTypeKind::Karakter); break;
        case TokenKind::KwTeks: result = context_.types().getBuiltinType(BuiltinTypeKind::Teks); break;
        case TokenKind::KwHampa: result = context_.types().getBuiltinType(BuiltinTypeKind::Hampa); break;
        case TokenKind::KwDesimalTetap: result = context_.types().getBuiltinObjectType("desimal_tetap"); break;
        case TokenKind::KwBilanganTanpaBatas: result = context_.types().getBuiltinObjectType("bilangan_tanpa_batas"); break;
        case TokenKind::KwBilanganKompleks: result = context_.types().getBuiltinObjectType("bilangan_kompleks"); break;
        case TokenKind::KwMatriks: result = context_.types().getBuiltinObjectType("matriks"); break;
        default:
            error(repr->range, "unsupported builtin type in semantic v1");
            break;
        }
        break;
    }

    case AstKind::NamedTypeRepr: {
        const auto* named = static_cast<NamedTypeRepr*>(repr);
        if (TypeVariableType* variable = lookupTypeVariable(named->name)) {
            result = variable;
        } else if (Symbol* symbol = context_.globalScope().lookup(named->name)) {
            if (symbol->type) {
                result = symbol->type;
            } else {
                error(repr->range, "name does not name a type: " + named->name);
            }
        } else if (named->name.find('.') != std::string::npos) {
            // Qualified enum case used as a type: Daftar_Tipe.OPSI_BILANGAN
            const std::string enumName = named->name.substr(0, named->name.find('.'));
            const std::string caseName = named->name.substr(named->name.find('.') + 1);
            Symbol* enumSymbol = context_.globalScope().lookup(enumName);
            auto* sum = enumSymbol ? dynamic_cast<SumType*>(enumSymbol->type) : nullptr;
            const SumType::Case* found = sum ? sum->findCase(caseName) : nullptr;
            if (found) {
                result = found->aliasType ? found->aliasType : static_cast<Type*>(sum);
            } else {
                error(repr->range, "unknown type: " + named->name);
            }
        } else {
            error(repr->range, "unknown type: " + named->name);
        }
        break;
    }

    case AstKind::PointerTypeRepr: {
        Type* pointee = resolve(static_cast<PointerTypeRepr*>(repr)->pointee);
        result = context_.types().getPointerType(pointee);
        break;
    }

    case AstKind::ArrayTypeRepr: {
        auto* array = static_cast<ArrayTypeRepr*>(repr);
        Type* current = resolve(array->elementType);
        if (array->dimensions.empty()) {
            // `larik dari X` without an explicit size is an unsized array.
            current = context_.types().getArrayType(current, std::nullopt);
        }
        for (std::size_t i = array->dimensions.size(); i > 0; --i) {
            (void)array->dimensions[i - 1];
            current = context_.types().getArrayType(current, std::nullopt);
        }
        result = current;
        break;
    }

    case AstKind::FunctionPointerTypeRepr: {
        auto* function = static_cast<FunctionPointerTypeRepr*>(repr);
        std::vector<FunctionParamType> params;
        for (const auto& [name, paramRepr] : function->parameters) {
            params.push_back(FunctionParamType{name, resolve(paramRepr)});
        }
        Type* returnType = function->returnType ? resolve(function->returnType)
                                                : context_.types().getBuiltinType(BuiltinTypeKind::Hampa);
        result = context_.types().getFunctionType(std::move(params), returnType, false);
        break;
    }

    case AstKind::CallTypeRepr: {
        const auto* call = static_cast<CallTypeRepr*>(repr);
        if (TypeVariableType* variable = lookupTypeVariable(call->name)) {
            result = variable;
            break;
        }
        Symbol* symbol = context_.globalScope().lookup(call->name);
        if (!symbol) {
            error(repr->range, "unknown type: " + call->name);
            break;
        }
        Type* base = symbol->type;
        // Collect explicit type arguments; value arguments (construction
        // parameters, e.g. desimal_tetap("1.5", skala = 2)) carry no type.
        std::vector<std::pair<std::string, Type*>> arguments;
        for (const TypeArgumentRepr* argument : call->arguments) {
            if (!argument || !argument->type) continue;
            arguments.emplace_back(argument->name, resolve(argument->type));
        }
        bool generic = false;
        if (auto* structure = dynamic_cast<StructType*>(base)) {
            generic = !structure->typeParams.empty();
        } else if (auto* sum = dynamic_cast<SumType*>(base)) {
            generic = !sum->typeParams.empty();
        } else if (auto* object = dynamic_cast<BuiltinObjectType*>(base)) {
            generic = object->isGeneric();
        }
        if (generic && !arguments.empty()) {
            result = context_.types().getGenericInstantiation(base, std::move(arguments));
        } else {
            result = base;
        }
        break;
    }

    case AstKind::ErrorTypeRepr:
        break;

    default:
        error(repr->range, "unsupported type repr in semantic v1");
        break;
    }

    context_.setResolvedType(repr, result);
    return result;
}

} // namespace rhc
