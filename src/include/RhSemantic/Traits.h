#pragma once

#include "RhSemantic/Type.h"

#include <string>

namespace rhc {

// Kategori C: satisfaction of built-in traits.
// Unknown/error/type-variable types are treated optimistically so that
// analysis can continue; concrete types are checked against the table.
inline bool satisfiesBuiltinTrait(Type* type, const std::string& trait) {
    if (!type || dynamic_cast<ErrorType*>(type) ||
        dynamic_cast<TypeVariableType*>(type)) {
        return true;
    }
    if (dynamic_cast<BuiltinType*>(type)) {
        if (trait == "Terbandingkan") return true; // all scalar builtins are comparable
        return false;
    }
    if (auto* sum = dynamic_cast<SumType*>(type)) {
        return trait == "Terbandingkan" && sum->isPlainEnum;
    }
    return false; // builtin objects & user structs: no trait impls yet
}

} // namespace rhc
