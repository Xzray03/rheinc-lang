#include "RhSemantic/Symbol.h"

#include <utility>

namespace rhc {

Symbol::Symbol(SymbolKind symbolKind, std::string symbolName, Type* symbolType,
               DeclNode* declaration, SourceRange symbolRange, Scope* parentScope)
    : kind(symbolKind), name(std::move(symbolName)), type(symbolType), decl(declaration),
      range(symbolRange), scope(parentScope) {}

} // namespace rhc
