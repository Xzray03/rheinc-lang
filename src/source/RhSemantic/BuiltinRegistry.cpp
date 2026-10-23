#include "RhSemantic/BuiltinRegistry.h"

#include <string>
#include <utility>
#include <vector>

namespace rhc {

void BuiltinRegistry::registerAll(SemanticContext& context) {
    registerBuiltinTypes(context);
    registerBuiltinFunctions(context);
}

} // namespace rhc
