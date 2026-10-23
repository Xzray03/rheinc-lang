#include "RhLexer/Lexer.h"

#include "RhLexer/OperatorTable.h"
namespace rhc {


Token Lexer::scanOperatorOrPunctuator() {
    const SourceLocation start = cursor_.offset();
    const std::string_view remaining = sourceManager_.getBuffer(file_).substr(cursor_.offset());

    for (std::size_t length = 3; length >= 1; --length) {
        if (remaining.size() < length) continue;
        const TokenKind kind = OperatorTable::lookup(remaining.substr(0, length));
        if (kind != TokenKind::Unknown) {
            cursor_.advance(length);
            return makeToken(kind, start, static_cast<std::uint32_t>(length));
        }
    }

    char32_t cp = 0;
    std::size_t length = 0;
    const bool valid = cursor_.peek(cp, length);
    cursor_.advance(length == 0 ? 1 : length);
    if (!valid) {
        error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
              "invalid UTF-8 sequence");
    } else {
        error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
              "unknown character");
    }
    return makeToken(TokenKind::Unknown, start,
                     static_cast<std::uint32_t>(cursor_.offset() - start),
                     TokenFlags::Invalid);
}

} // namespace rhc
