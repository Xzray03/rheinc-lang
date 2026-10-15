#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace rhc {

struct EnumDecl;
struct StructDecl;
struct ExprNode;

enum class BuiltinTypeKind {
    Bilangan,
    Bilangan8,
    Bilangan16,
    Bilangan32,
    Bilangan64,
    BilanganTanpaTanda8,
    BilanganTanpaTanda16,
    BilanganTanpaTanda32,
    BilanganTanpaTanda64,
    Desimal,
    Logika,
    Karakter,
    Teks,
    Hampa,
};

enum class TypeKind {
    Builtin,
    Pointer,
    Array,
    Slice,
    Function,
    Struct,
    Sum,
    GenericInstantiation,
    TypeVariable,
    Error,
};

class Type {
public:
    explicit Type(TypeKind kind) : kind_(kind) {}
    virtual ~Type() = default;
    TypeKind kind() const { return kind_; }

private:
    TypeKind kind_;
};

class BuiltinType final : public Type {
public:
    BuiltinTypeKind builtinKind;
    std::string name;
    unsigned bits;
    bool isSigned;

    BuiltinType(BuiltinTypeKind kind, std::string typeName, unsigned bitWidth, bool signedType)
        : Type(TypeKind::Builtin), builtinKind(kind), name(std::move(typeName)),
          bits(bitWidth), isSigned(signedType) {}
};

class PointerType final : public Type {
public:
    Type* pointee;
    explicit PointerType(Type* pointeeType) : Type(TypeKind::Pointer), pointee(pointeeType) {}
};

class ArrayType final : public Type {
public:
    Type* elementType;
    std::optional<std::uint64_t> size;
    ArrayType(Type* element, std::optional<std::uint64_t> arraySize)
        : Type(TypeKind::Array), elementType(element), size(arraySize) {}
};

struct FunctionParamType {
    std::string name;
    Type* type;
};

class SliceType final : public Type {
public:
    Type* elementType;
    explicit SliceType(Type* element) : Type(TypeKind::Slice), elementType(element) {}
};

class TypeVariableType;

class FunctionType final : public Type {
public:
    std::vector<FunctionParamType> params;
    Type* returnType;
    bool isVariadic;
    std::vector<TypeVariableType*> typeParams;
    // Trait constraints per type variable (Kategori C).
    std::vector<std::pair<TypeVariableType*, std::vector<std::string>>> constraints;

    FunctionType(std::vector<FunctionParamType> parameters, Type* result, bool variadic)
        : Type(TypeKind::Function), params(std::move(parameters)), returnType(result),
          isVariadic(variadic) {}
};

class TypeVariableType;

class StructType final : public Type {
public:
    StructDecl* decl;
    std::vector<struct Symbol*> fields;
    std::vector<TypeVariableType*> typeParams;
    std::vector<std::pair<TypeVariableType*, std::vector<std::string>>> constraints;
    explicit StructType(StructDecl* structDecl) : Type(TypeKind::Struct), decl(structDecl) {}
};

class TypeVariableType;

class BuiltinObjectType final : public Type {
public:
    std::string name;
    std::unordered_map<std::string, Type*> members;
    std::vector<TypeVariableType*> typeParams;

    explicit BuiltinObjectType(std::string objectName)
        : Type(TypeKind::Builtin), name(std::move(objectName)) {}
    bool isGeneric() const { return !typeParams.empty(); }
};


// Type variable for generic declarations, e.g. T in `generik T fungsi maks(...)`.
class TypeVariableType final : public Type {
public:
    std::string name;

    explicit TypeVariableType(std::string typeName)
        : Type(TypeKind::TypeVariable), name(std::move(typeName)) {}
};

// Sum type: cacahan (plain enum) and cacahan_sum (tagged union with payloads).
class SumType final : public Type {
public:
    struct Field {
        std::string name; // may be empty for positional payloads, e.g. ADA(T)
        Type* type = nullptr;
    };
    struct Case {
        std::string name;
        std::vector<Field> fields;
        ExprNode* aliasValue = nullptr; // plain enum: = <nilai>; or alias to another type
        Type* aliasType = nullptr;      // resolved when aliasValue denotes a type
        // Evaluated constant value for plain enum cases (codegen-ready).
        std::int64_t enumValue = 0;
        bool hasEnumValue = false;
    };

    std::string name;
    EnumDecl* decl;
    bool isPlainEnum;
    std::vector<TypeVariableType*> typeParams;
    std::vector<Case> cases;
    // Trait constraints per type variable, for generic cacahan_sum.
    std::vector<std::pair<TypeVariableType*, std::vector<std::string>>> constraints;

    SumType(std::string sumName, EnumDecl* enumDecl, bool plain,
            std::vector<TypeVariableType*> params)
        : Type(TypeKind::Sum), name(std::move(sumName)), decl(enumDecl),
          isPlainEnum(plain), typeParams(std::move(params)) {}

    const Case* findCase(const std::string& caseName) const {
        for (const Case& c : cases) {
            if (c.name == caseName) return &c;
        }
        return nullptr;
    }
};

// Instantiation of a generic type, e.g. Opsi(dengan T = bilangan) or Pasangan(teks).
class GenericInstantiationType final : public Type {
public:
    Type* generic; // StructType, SumType, BuiltinObjectType, or FunctionType with typeParams
    std::vector<std::pair<std::string, Type*>> arguments; // may be positional (empty name)

    GenericInstantiationType(Type* genericType,
                             std::vector<std::pair<std::string, Type*>> args)
        : Type(TypeKind::GenericInstantiation), generic(genericType),
          arguments(std::move(args)) {}
};

class ErrorType final : public Type {
public:
    ErrorType() : Type(TypeKind::Error) {}
};

} // namespace rhc
