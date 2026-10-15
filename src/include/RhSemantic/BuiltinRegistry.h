#pragma once

#include "RhSemantic/SemanticContext.h"

namespace rhc {

class BuiltinRegistry {
public:
    void registerAll(SemanticContext& context);

private:
    void registerBuiltinTypes(SemanticContext& context);
    void registerBuiltinFunctions(SemanticContext& context);
};

} // namespace rhc
