#pragma once

#include "RhSemantic/Symbol.h"

#include <vector>

namespace rhc {

struct SemanticModule {
    std::vector<Symbol*> functions;
    std::vector<Symbol*> globals;
    std::vector<Symbol*> structs;
};

} // namespace rhc
