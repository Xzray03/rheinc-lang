#pragma once

#include "RhParser/Decl.h"
#include "RhSemantic/Type.h"

#include <cstdint>
#include <string>

namespace rhc {

// E-4: codegen symbol naming for types (generic-instantiation safe).
inline std::string mangleType(Type* type) {
    if (!type) return "v";
    if (auto* builtin = dynamic_cast<BuiltinType*>(type)) {
        switch (builtin->builtinKind) {
            case BuiltinTypeKind::Hampa: return "hampa";
            case BuiltinTypeKind::Bilangan: return "bilangan";
            case BuiltinTypeKind::Teks: return "teks";
            case BuiltinTypeKind::Logika: return "logika";
            case BuiltinTypeKind::Karakter: return "karakter";
            case BuiltinTypeKind::Desimal: return "desimal";
            default: return "builtin";
        }
    }
    if (auto* pointer = dynamic_cast<PointerType*>(type)) {
        return "P_" + mangleType(pointer->pointee);
    }
    if (auto* array = dynamic_cast<ArrayType*>(type)) {
        std::string base = "A_" + mangleType(array->elementType);
        if (array->size) base += "_" + std::to_string(*array->size);
        return base;
    }
    if (auto* slice = dynamic_cast<SliceType*>(type)) {
        return "S_" + mangleType(slice->elementType);
    }
    if (auto* function = dynamic_cast<FunctionType*>(type)) {
        std::string result = "F_";
        for (const FunctionParamType& param : function->params) {
            result += mangleType(param.type) + "_";
        }
        if (function->isVariadic) result += "variadic_";
        result += "R_" + mangleType(function->returnType);
        return result;
    }
    if (auto* variable = dynamic_cast<TypeVariableType*>(type)) {
        return "T_" + variable->name;
    }
    if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(type)) {
        std::string base = mangleType(instantiation->generic);
        base += "__I_";
        bool first = true;
        for (const auto& [name, argument] : instantiation->arguments) {
            if (!first) base += "_";
            if (!name.empty()) base += name + "_";
            base += mangleType(argument);
            first = false;
        }
        return base;
    }
    if (auto* sum = dynamic_cast<SumType*>(type)) {
        return "C_" + sum->name; // cacahan
    }
    if (auto* structure = dynamic_cast<StructType*>(type)) {
        return "X_" + structure->decl->name; // struktur
    }
    if (auto* object = dynamic_cast<BuiltinObjectType*>(type)) {
        return "O_" + object->name;
    }
    return "unknown";
}

} // namespace rhc
