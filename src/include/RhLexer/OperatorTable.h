#pragma once

#include "RhLexer/TokenKind.h"

#include <string_view>

namespace rhc {

class OperatorTable {
public:
    static TokenKind lookup(std::string_view text);
};

} // namespace rhc
