#include "RhLexer/Lexer.h"

#include "RhLexer/LexerCharClass.h"
#include "RhCore/Utf8.h"
namespace rhc {


bool Lexer::skipCommentIfAny(bool atTokenStart) {
    if (!atTokenStart || !cursor_.startsWith("--")) {
        return false;
    }

    if (cursor_.startsWith("--[")) {
        return trySkipBlockComment();
    }

    // Line comment requires whitespace after '--'.
    const std::size_t afterMarker = cursor_.offset() + 2;
    char32_t cp = 0;
    std::size_t length = 0;
    const std::string_view buffer = sourceManager_.getBuffer(file_);
    utf8::decode(buffer.data(), buffer.size(), afterMarker, cp, length);
    if (cp != utf8::SentinelEOF && (isHorizontalWhitespace(cp) || utf8::isNewline(cp))) {
        return trySkipLineComment();
    }
    return false;
}


bool Lexer::trySkipLineComment() {
    const SourceLocation start = cursor_.offset();
    cursor_.advance(2);
    while (!cursor_.eof() && !atLogicalNewline()) {
        const SourceLocation charLoc = cursor_.offset();
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (length == 0) break;
        if (!valid) {
            error(charLoc, 1, "invalid UTF-8 sequence in comment");
        }
        cursor_.advance(length);
    }
    (void)start;
    return true;
}


bool Lexer::trySkipBlockComment() {
    const SourceLocation start = cursor_.offset();
    cursor_.advance(3); // --[
    while (!cursor_.eof()) {
        if (cursor_.startsWith("]--")) {
            cursor_.advance(3);
            return true;
        }
        const SourceLocation charLoc = cursor_.offset();
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (length == 0) break;
        if (!valid) {
            error(charLoc, 1, "invalid UTF-8 sequence in comment");
        }
        cursor_.advance(length);
    }
    error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
          "unterminated block comment");
    return true;
}

} // namespace rhc
