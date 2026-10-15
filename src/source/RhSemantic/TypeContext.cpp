#include "RhSemantic/TypeContext.h"

#include <algorithm>
#include <utility>

namespace rhc {

BuiltinObjectType* TypeContext::getBuiltinObjectType(std::string name) {
    for (auto& type : types_) {
        if (auto* object = dynamic_cast<BuiltinObjectType*>(type.get());
            object && object->name == name) {
            return object;
        }
    }
    auto type = std::make_unique<BuiltinObjectType>(std::move(name));
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

BuiltinType* TypeContext::getBuiltinType(BuiltinTypeKind kind) {
    for (auto& type : types_) {
        if (auto* builtin = dynamic_cast<BuiltinType*>(type.get());
            builtin && builtin->builtinKind == kind) {
            return builtin;
        }
    }

    std::string name;
    unsigned bits = 0;
    bool isSigned = false;
    switch (kind) {
    case BuiltinTypeKind::Bilangan: name = "bilangan"; bits = 64; isSigned = true; break;
    case BuiltinTypeKind::Bilangan8: name = "bilangan8"; bits = 8; isSigned = true; break;
    case BuiltinTypeKind::Bilangan16: name = "bilangan16"; bits = 16; isSigned = true; break;
    case BuiltinTypeKind::Bilangan32: name = "bilangan32"; bits = 32; isSigned = true; break;
    case BuiltinTypeKind::Bilangan64: name = "bilangan64"; bits = 64; isSigned = true; break;
    case BuiltinTypeKind::BilanganTanpaTanda8: name = "bilangan_tanpa_tanda8"; bits = 8; isSigned = false; break;
    case BuiltinTypeKind::BilanganTanpaTanda16: name = "bilangan_tanpa_tanda16"; bits = 16; isSigned = false; break;
    case BuiltinTypeKind::BilanganTanpaTanda32: name = "bilangan_tanpa_tanda32"; bits = 32; isSigned = false; break;
    case BuiltinTypeKind::BilanganTanpaTanda64: name = "bilangan_tanpa_tanda64"; bits = 64; isSigned = false; break;
    case BuiltinTypeKind::Desimal: name = "desimal"; bits = 64; isSigned = true; break;
    case BuiltinTypeKind::Logika: name = "logika"; bits = 8; isSigned = false; break;
    case BuiltinTypeKind::Karakter: name = "karakter"; bits = 32; isSigned = false; break;
    case BuiltinTypeKind::Teks: name = "teks"; bits = 0; isSigned = false; break;
    case BuiltinTypeKind::Hampa: name = "hampa"; bits = 0; isSigned = false; break;
    }

    auto type = std::make_unique<BuiltinType>(kind, std::move(name), bits, isSigned);
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

PointerType* TypeContext::getPointerType(Type* pointee) {
    for (auto& type : types_) {
        if (auto* pointer = dynamic_cast<PointerType*>(type.get());
            pointer && pointer->pointee == pointee) {
            return pointer;
        }
    }
    auto type = std::make_unique<PointerType>(pointee);
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

ArrayType* TypeContext::getArrayType(Type* elementType, std::optional<std::uint64_t> size) {
    for (auto& type : types_) {
        if (auto* array = dynamic_cast<ArrayType*>(type.get());
            array && array->elementType == elementType && array->size == size) {
            return array;
        }
    }
    auto type = std::make_unique<ArrayType>(elementType, size);
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

SliceType* TypeContext::getSliceType(Type* elementType) {
    for (auto& type : types_) {
        if (auto* slice = dynamic_cast<SliceType*>(type.get());
            slice && slice->elementType == elementType) {
            return slice;
        }
    }
    auto type = std::make_unique<SliceType>(elementType);
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

FunctionType* TypeContext::getFunctionType(std::vector<FunctionParamType> params,
                                           Type* returnType,
                                           bool isVariadic) {
    return getFunctionType(std::move(params), returnType, isVariadic, {});
}

FunctionType* TypeContext::getFunctionType(std::vector<FunctionParamType> params,
                                           Type* returnType,
                                           bool isVariadic,
                                           std::vector<TypeVariableType*> typeParams) {
    auto sameParams = [&](FunctionType* function) {
        if (function->returnType != returnType || function->isVariadic != isVariadic ||
            function->params.size() != params.size() ||
            function->typeParams.size() != typeParams.size()) {
            return false;
        }
        for (std::size_t i = 0; i < typeParams.size(); ++i) {
            if (function->typeParams[i] != typeParams[i]) return false;
        }
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (function->params[i].name != params[i].name ||
                function->params[i].type != params[i].type) {
                return false;
            }
        }
        return true;
    };
    for (auto& type : types_) {
        if (auto* function = dynamic_cast<FunctionType*>(type.get()); function && sameParams(function)) {
            return function;
        }
    }
    auto type = std::make_unique<FunctionType>(std::move(params), returnType, isVariadic);
    type->typeParams = std::move(typeParams);
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

TypeVariableType* TypeContext::getTypeVariable(const std::string& name) {
    for (auto& type : types_) {
        if (auto* variable = dynamic_cast<TypeVariableType*>(type.get());
            variable && variable->name == name) {
            return variable;
        }
    }
    auto type = std::make_unique<TypeVariableType>(name);
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

SumType* TypeContext::createSumType(std::string name, EnumDecl* decl, bool isPlainEnum,
                                    std::vector<TypeVariableType*> typeParams) {
    auto type = std::make_unique<SumType>(std::move(name), decl, isPlainEnum,
                                          std::move(typeParams));
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

GenericInstantiationType* TypeContext::getGenericInstantiation(
    Type* generic, std::vector<std::pair<std::string, Type*>> arguments) {
    auto sameArguments = [&](GenericInstantiationType* instantiation) {
        if (instantiation->generic != generic ||
            instantiation->arguments.size() != arguments.size()) {
            return false;
        }
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (instantiation->arguments[i].first != arguments[i].first ||
                instantiation->arguments[i].second != arguments[i].second) {
                return false;
            }
        }
        return true;
    };
    for (auto& type : types_) {
        if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(type.get());
            instantiation && sameArguments(instantiation)) {
            return instantiation;
        }
    }
    auto type = std::make_unique<GenericInstantiationType>(generic, std::move(arguments));
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

StructType* TypeContext::createStructType(StructDecl* decl) {
    auto type = std::make_unique<StructType>(decl);
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

ErrorType* TypeContext::getErrorType() {
    for (auto& type : types_) {
        if (auto* error = dynamic_cast<ErrorType*>(type.get())) return error;
    }
    auto type = std::make_unique<ErrorType>();
    auto* result = type.get();
    types_.push_back(std::move(type));
    return result;
}

} // namespace rhc
