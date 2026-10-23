#include "RhLexer/Lexer.h"

#include "RhLexer/LexerCharClass.h"
namespace rhc {


Token Lexer::scanCharConstant() {
    const SourceLocation start = cursor_.offset();
    bool invalid = false;
    cursor_.advance(1); // opening quote

    if (cursor_.eof() || atLogicalNewline()) {
        error(start, 1, "unterminated char literal");
        return makeToken(TokenKind::CharConstant, start, 1, TokenFlags::Invalid);
    }

    if (sourceManager_.getBuffer(file_)[cursor_.offset()] == '\'') {
        cursor_.advance(1);
        return makeToken(TokenKind::CharConstant, start, 2, TokenFlags::None);
    }

    // First value.
    if (sourceManager_.getBuffer(file_)[cursor_.offset()] == '\\') {
        const SourceLocation escapeLoc = cursor_.offset();
        cursor_.advance(1);
        if (cursor_.eof() || atLogicalNewline()) {
            error(escapeLoc, static_cast<std::uint32_t>(cursor_.offset() - escapeLoc),
                  "unterminated escape sequence in char literal");
            return makeToken(TokenKind::CharConstant, start,
                             static_cast<std::uint32_t>(cursor_.offset() - start),
                             TokenFlags::Invalid);
        }
        char32_t escape = 0;
        std::size_t escapeLength = 0;
        const bool escapeValid = cursor_.peek(escape, escapeLength);
        if (!escapeValid) {
            invalid = true;
            error(cursor_.offset(), 1, "invalid UTF-8 sequence in char literal");
            cursor_.advance(1);
        } else {
            if (!isValidEscape(escape)) {
                invalid = true;
                error(escapeLoc, static_cast<std::uint32_t>(escapeLength + 1),
                      "invalid escape sequence in char literal");
            }
            cursor_.advance(escapeLength);
        }
    } else {
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (!valid) {
            invalid = true;
            error(cursor_.offset(), 1, "invalid UTF-8 sequence in char literal");
            cursor_.advance(1);
        } else {
            cursor_.advance(length);
        }
    }

    if (cursor_.eof()) {
        error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
              "unterminated char literal");
        return makeToken(TokenKind::CharConstant, start,
                         static_cast<std::uint32_t>(cursor_.offset() - start),
                         TokenFlags::Invalid);
    }

    if (atLogicalNewline()) {
        error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
              "unterminated char literal");
        return makeToken(TokenKind::CharConstant, start,
                         static_cast<std::uint32_t>(cursor_.offset() - start),
                         TokenFlags::Invalid);
    }

    if (sourceManager_.getBuffer(file_)[cursor_.offset()] == '\'') {
        cursor_.advance(1);
        TokenFlags flags = invalid ? TokenFlags::Invalid : TokenFlags::None;
        return makeToken(TokenKind::CharConstant, start,
                         static_cast<std::uint32_t>(cursor_.offset() - start), flags);
    }

    // More than one value in char literal: consume until closing quote/newline/EOF.
    invalid = true;
    const SourceLocation extraLoc = cursor_.offset();
    while (!cursor_.eof() && !atLogicalNewline() &&
           sourceManager_.getBuffer(file_)[cursor_.offset()] != '\'') {
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (!valid) {
            error(cursor_.offset(), 1, "invalid UTF-8 sequence in char literal");
            cursor_.advance(1);
        } else {
            cursor_.advance(length);
        }
    }
    error(extraLoc, static_cast<std::uint32_t>(cursor_.offset() - extraLoc),
          "char literal contains more than one value");

    if (!cursor_.eof() && sourceManager_.getBuffer(file_)[cursor_.offset()] == '\'') {
        cursor_.advance(1);
    } else {
        error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
              "unterminated char literal");
    }

    return makeToken(TokenKind::CharConstant, start,
                     static_cast<std::uint32_t>(cursor_.offset() - start),
                     TokenFlags::Invalid);
}

} // namespace rhc
