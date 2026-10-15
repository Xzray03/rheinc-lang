#pragma once

#include "RhCore/SourceLocation.h"
#include "RhLexer/IdentifierInfo.h"
#include "RhLexer/TokenFlags.h"
#include "RhLexer/TokenKind.h"

#include <cstdint>

namespace rhc {

struct Token {
    TokenKind kind = TokenKind::Unknown;
    TokenFlags flags = TokenFlags::None;
    SourceLocation loc = 0;
    std::uint32_t length = 0;
    const IdentifierInfo* ident = nullptr;
};

} // namespace rhc
