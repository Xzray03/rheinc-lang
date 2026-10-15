#pragma once

#include "RhSemantic/Type.h"

#include <memory>
#include <optional>
#include <vector>

namespace rhc {

class TypeContext {
public:
    BuiltinObjectType* getBuiltinObjectType(std::string name);
    BuiltinType* getBuiltinType(BuiltinTypeKind kind);
    PointerType* getPointerType(Type* pointee);
    ArrayType* getArrayType(Type* elementType, std::optional<std::uint64_t> size);
    SliceType* getSliceType(Type* elementType);
    FunctionType* getFunctionType(std::vector<FunctionParamType> params,
                                  Type* returnType,
                                  bool isVariadic);
    FunctionType* getFunctionType(std::vector<FunctionParamType> params,
                                  Type* returnType,
                                  bool isVariadic,
                                  std::vector<TypeVariableType*> typeParams);
    StructType* createStructType(StructDecl* decl);
    TypeVariableType* getTypeVariable(const std::string& name);
    SumType* createSumType(std::string name, EnumDecl* decl, bool isPlainEnum,
                           std::vector<TypeVariableType*> typeParams);
    GenericInstantiationType* getGenericInstantiation(
        Type* generic, std::vector<std::pair<std::string, Type*>> arguments);
    ErrorType* getErrorType();

private:
    std::vector<std::unique_ptr<Type>> types_;
};

} // namespace rhc
