#include "RhLexer/Lexer.h"

#include "RhLexer/LexerCharClass.h"
namespace rhc {


Token Lexer::scanIdentifierOrKeyword(bool& wordLike) {
    const SourceLocation start = cursor_.offset();
    bool invalid = false;
    while (!cursor_.eof()) {
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (length == 0) break;
        // An invalid byte inside a word stays part of the identifier so it is
        // reported (and flagged) here instead of splitting the identifier apart.
        if (valid && !isIdentifierContinue(cp)) break;
        if (!valid) {
            invalid = true;
            error(cursor_.offset(), 1, "invalid UTF-8 sequence in identifier");
        }
        cursor_.advance(length);
    }

    const std::string_view text = sourceManager_.getText(
        file_, start, static_cast<std::uint32_t>(cursor_.offset() - start));
    IdentifierInfo& info = identifierTable_.intern(text);
    wordLike = true;
    TokenFlags flags = invalid ? TokenFlags::Invalid : TokenFlags::None;
    if (info.isReservedKeyword()) {
        return makeToken(info.getReservedKeywordKind(), start,
                         static_cast<std::uint32_t>(cursor_.offset() - start), flags, &info);
    }
    return makeToken(TokenKind::Identifier, start,
                     static_cast<std::uint32_t>(cursor_.offset() - start), flags, &info);
}

} // namespace rhc
