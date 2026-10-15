#pragma once

#include "RhCore/SourceLocation.h"
#include "RhParser/AstNode.h"
#include "RhSemantic/Type.h"

#include <string>

namespace rhc {

struct DeclNode;
class Scope;

enum class SymbolKind {
    Function,
    Procedure,
    GlobalVar,
    LocalVar,
    Param,
    Struct,
    StructField,
    Enum,
    EnumConstant,
    BuiltinType,
    BuiltinFunction,
};

class Symbol {
public:
    SymbolKind kind;
    std::string name;
    Type* type;
    DeclNode* decl;
    SourceRange range;
    Scope* scope;

    Symbol(SymbolKind symbolKind, std::string symbolName, Type* symbolType,
           DeclNode* declaration, SourceRange symbolRange, Scope* parentScope);
};


// True for symbols introduced by BuiltinRegistry (declaration-less builtins);
// user declarations are allowed to shadow these.
inline bool isBuiltinSymbol(const Symbol& symbol) {
    return symbol.decl == nullptr;
}

} // namespace rhc
