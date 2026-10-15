#pragma once

#include "RhSemantic/Symbol.h"
#include "RhSemantic/Type.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rhc {

// E-3: type layout (size & alignment in bytes) computed from the type graph.
// Codegen-ready: builtin ABI table + struct field offsets with padding.
struct TypeLayout {
    std::uint64_t size = 0;
    std::uint64_t align = 1;
};

inline std::uint64_t alignUp(std::uint64_t value, std::uint64_t align) {
    return align <= 1 ? value : (value + align - 1) / align * align;
}

// Computes the layout of `type`. Pointers, function values and builtin object
// references are pointer-sized (8). Unsized arrays report size 0.
// Sets `*infinite` when the type has no finite size (e.g. a struct that
// contains itself by value); `visiting` must be an empty vector per top call.
TypeLayout computeLayout(Type* type, std::unordered_map<Type*, TypeLayout>& memo,
                         std::vector<Type*>& visiting, bool* infinite) {
    if (!type) return {};
    if (dynamic_cast<ErrorType*>(type)) return {};
    auto it = memo.find(type);
    if (it != memo.end()) return it->second;

    for (Type* v : visiting) {
        if (v == type) {
            // A cycle not broken by a pointer boundary: infinite size.
            if (infinite) *infinite = true;
            return {};
        }
    }
    visiting.push_back(type);

    TypeLayout layout;
    if (auto* builtin = dynamic_cast<BuiltinType*>(type)) {
        switch (builtin->builtinKind) {
            case BuiltinTypeKind::Hampa: layout = {0, 1}; break;
            case BuiltinTypeKind::Logika: layout = {1, 1}; break;
            case BuiltinTypeKind::Karakter: layout = {4, 4}; break;
            case BuiltinTypeKind::Bilangan:
            case BuiltinTypeKind::Desimal: layout = {8, 8}; break;
            case BuiltinTypeKind::Teks: layout = {16, 8}; break; // ptr + length
            default: layout = {8, 8}; break;
        }
    } else if (dynamic_cast<PointerType*>(type) ||
               dynamic_cast<FunctionType*>(type) ||
               dynamic_cast<BuiltinObjectType*>(type)) {
        layout = {8, 8}; // references / code pointers
    } else if (auto* array = dynamic_cast<ArrayType*>(type)) {
        TypeLayout element = computeLayout(array->elementType, memo, visiting, infinite);
        if (array->size && *array->size > 0) {
            layout = {alignUp(element.size, element.align) * static_cast<std::uint64_t>(*array->size),
                      element.align};
        } else {
            layout = {0, element.align}; // unsized
        }
    } else if (dynamic_cast<SliceType*>(type)) {
        layout = {16, 8}; // ptr + length
    } else if (auto* instantiation = dynamic_cast<GenericInstantiationType*>(type)) {
        layout = computeLayout(instantiation->generic, memo, visiting, infinite);
    } else if (auto* sum = dynamic_cast<SumType*>(type)) {
        if (sum->isPlainEnum) {
            layout = {8, 8}; // bilangan backing
        } else {
            // tag + largest payload
            std::uint64_t payload = 0, payloadAlign = 1;
            for (const SumType::Case& c : sum->cases) {
                std::uint64_t off = 0, al = 1;
                for (const SumType::Field& f : c.fields) {
                    TypeLayout fl = computeLayout(f.type, memo, visiting, infinite);
                    al = std::max(al, fl.align);
                    off = alignUp(off, fl.align) + fl.size;
                }
                payload = std::max(payload, off);
                payloadAlign = std::max(payloadAlign, al);
            }
            payloadAlign = std::max<std::uint64_t>(payloadAlign, 8);
            layout = {alignUp(8, payloadAlign) + alignUp(payload, payloadAlign), payloadAlign};
        }
    } else if (auto* structure = dynamic_cast<StructType*>(type)) {
        std::uint64_t offset = 0, maxAlign = 1;
        for (Symbol* field : structure->fields) {
            if (!field || !field->type) continue;
            TypeLayout fl = computeLayout(field->type, memo, visiting, infinite);
            maxAlign = std::max(maxAlign, fl.align);
            offset = alignUp(offset, fl.align) + fl.size;
        }
        layout = {alignUp(offset, maxAlign), maxAlign};
    } else {
        layout = {8, 8};
    }

    visiting.pop_back();
    memo[type] = layout;
    return layout;
}

inline TypeLayout computeLayout(Type* type, bool* infinite = nullptr) {
    std::unordered_map<Type*, TypeLayout> memo;
    std::vector<Type*> visiting;
    return computeLayout(type, memo, visiting, infinite);
}

// Struct field offsets (same order as StructType::fields).
inline std::vector<std::uint64_t>
structFieldOffsets(StructType* structure, std::unordered_map<Type*, TypeLayout>& memo) {
    std::vector<std::uint64_t> offsets;
    std::uint64_t offset = 0;
    std::vector<Type*> visiting;
    for (Symbol* field : structure->fields) {
        if (!field || !field->type) {
            offsets.push_back(offset);
            continue;
        }
        TypeLayout fl = computeLayout(field->type, memo, visiting, nullptr);
        offset = alignUp(offset, fl.align);
        offsets.push_back(offset);
        offset += fl.size;
    }
    return offsets;
}

} // namespace rhc
