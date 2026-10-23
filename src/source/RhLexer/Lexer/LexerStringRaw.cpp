#include "RhLexer/Lexer.h"

namespace rhc {


bool Lexer::tryScanRawString(Token& result) {
    if (!cursor_.startsWith("Raw(\"")) {
        return false;
    }

    const SourceLocation start = cursor_.offset();
    cursor_.advance(5); // Raw("
    bool invalid = false;

    while (!cursor_.eof()) {
        if (cursor_.startsWith("\")")) {
            cursor_.advance(2);
            TokenFlags flags = TokenFlags::RawLiteral;
            if (invalid) flags = flags | TokenFlags::Invalid;
            result = makeToken(TokenKind::StringLiteral, start,
                               static_cast<std::uint32_t>(cursor_.offset() - start), flags);
            return true;
        }

        const SourceLocation charLoc = cursor_.offset();
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (length == 0) break;
        if (!valid) {
            invalid = true;
            error(charLoc, 1, "invalid UTF-8 sequence in raw string literal");
            cursor_.advance(1);
            continue;
        }
        cursor_.advance(length);
    }

    invalid = true;
    error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
          "unterminated raw string literal");
    result = makeToken(TokenKind::StringLiteral, start,
                       static_cast<std::uint32_t>(cursor_.offset() - start),
                       TokenFlags::RawLiteral | TokenFlags::Invalid);
    return true;
}

} // namespace rhc
