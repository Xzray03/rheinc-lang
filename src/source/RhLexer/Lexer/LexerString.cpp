#include "RhLexer/Lexer.h"

#include "RhLexer/LexerCharClass.h"
namespace rhc {


Token Lexer::scanStringLiteral() {
    const SourceLocation start = cursor_.offset();
    bool invalid = false;
    cursor_.advance(1); // opening quote

    while (!cursor_.eof()) {
        if (atLogicalNewline()) {
            invalid = true;
            error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
                  "unterminated string literal");
            break;
        }

        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (length == 0) break;
        if (cp == '"') {
            cursor_.advance(length);
            TokenFlags flags = invalid ? TokenFlags::Invalid : TokenFlags::None;
            return makeToken(TokenKind::StringLiteral, start,
                             static_cast<std::uint32_t>(cursor_.offset() - start), flags);
        }
        if (!valid) {
            invalid = true;
            error(cursor_.offset(), 1, "invalid UTF-8 sequence in string literal");
            cursor_.advance(1);
            continue;
        }
        if (cp == '\\') {
            const SourceLocation escapeLoc = cursor_.offset();
            cursor_.advance(length);
            if (cursor_.eof() || atLogicalNewline()) {
                invalid = true;
                error(escapeLoc, static_cast<std::uint32_t>(cursor_.offset() - escapeLoc),
                      "unterminated escape sequence in string literal");
                break;
            }
            char32_t escape = 0;
            std::size_t escapeLength = 0;
            const bool escapeValid = cursor_.peek(escape, escapeLength);
            if (!escapeValid) {
                invalid = true;
                error(cursor_.offset(), 1, "invalid UTF-8 sequence in string literal");
                cursor_.advance(1);
                continue;
            }
            if (!isValidEscape(escape)) {
                invalid = true;
                error(escapeLoc, static_cast<std::uint32_t>(escapeLength + 1),
                      "invalid escape sequence in string literal");
            }
            cursor_.advance(escapeLength);
            continue;
        }
        cursor_.advance(length);
    }

    if (cursor_.eof()) {
        invalid = true;
        error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
              "unterminated string literal");
    }

    TokenFlags flags = invalid ? TokenFlags::Invalid : TokenFlags::None;
    return makeToken(TokenKind::StringLiteral, start,
                     static_cast<std::uint32_t>(cursor_.offset() - start), flags);
}

} // namespace rhc
