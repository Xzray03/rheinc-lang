#pragma once

#include "RhLexer/Token.h"

#include <string>
#include <vector>

namespace rhc {

struct MacroDefinition {
    std::string name;
    bool isFunctionLike = false;
    bool isVariadic = false;
    std::vector<std::string> parameters;
    std::vector<Token> body;
};

} // namespace rhc
