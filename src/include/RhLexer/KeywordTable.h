#pragma once

#include "RhLexer/IdentifierInfo.h"

#include <string_view>

namespace rhc {

struct KeywordEntry {
    std::string_view name;
    TokenKind tokenKind;
    ContextualKeywordID contextualID;
};

class KeywordTable {
public:
    static const KeywordEntry* lookup(std::string_view name);
};

} // namespace rhc
