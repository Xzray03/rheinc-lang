#pragma once

#include "RhCore/DiagnosticEngine.h"
#include "RhParser/TypeRepr.h"
#include "RhSemantic/SemanticContext.h"

#include <vector>

namespace rhc {

class TypeResolver {
public:
    TypeResolver(SemanticContext& context, DiagnosticEngine& diagnostics);

    Type* resolve(TypeReprNode* repr);

    // Opens/closes a scope of type variables while collecting generic declarations.
    void pushTypeVariables(const std::vector<std::string>& names);
    void popTypeVariables();
    bool insideTypeVariableScope() const { return !typeVarScopes_.empty(); }
    std::vector<TypeVariableType*> currentTypeVariables() const;
    TypeVariableType* lookupTypeVariable(const std::string& name) const;

private:
    void error(SourceRange range, const std::string& message);

    SemanticContext& context_;
    DiagnosticEngine& diagnostics_;
    std::vector<std::vector<TypeVariableType*>> typeVarScopes_;
};

} // namespace rhc
