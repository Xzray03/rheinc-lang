#include "RhLexer/Lexer.h"

#include "RhCore/Utf8.h"
#include "RhLexer/OperatorTable.h"

#include <string>

namespace rhc {

Lexer::Lexer(FileID file, const SourceManager& sourceManager,
             IdentifierTable& identifierTable, DiagnosticEngine& diagnostics,
             const LangOptions& options)
    : file_(file), sourceManager_(sourceManager), identifierTable_(identifierTable),
      diagnostics_(diagnostics), options_(options), cursor_(sourceManager.getBuffer(file)) {}

Token Lexer::lex() {
    return lexImpl(false);
}

Token Lexer::lexRaw() {
    return lexImpl(true);
}

Token Lexer::lexImpl(bool rawMode) {
    if (emittedEOF_) {
        return makeToken(TokenKind::Eof, cursor_.offset(), 0,
                         rawMode ? TokenFlags::RawMode : TokenFlags::None);
    }

    TokenFlags commonFlags = rawMode ? TokenFlags::RawMode : TokenFlags::None;
    bool hadWhitespace = false;
    bool startOfLine = cursor_.offset() == 0;

    while (true) {
        if (rawMode && atLogicalNewline()) {
            const SourceLocation start = cursor_.offset();
            consumeLogicalNewline();
            Token token = makeToken(TokenKind::EndOfDirective, start,
                                    static_cast<std::uint32_t>(cursor_.offset() - start),
                                    TokenFlags::EndOfDirective);
            token.flags = token.flags | commonFlags;
            prevWordLike_ = false;
            return token;
        }

        // Cooked mode skips logical newlines as whitespace. Raw mode emits EndOfDirective.
        if (!rawMode && atLogicalNewline()) {
            consumeLogicalNewline();
            hadWhitespace = true;
            startOfLine = true;
            continue;
        }

        const std::size_t beforeSpace = cursor_.offset();
        skipHorizontalWhitespace();
        if (cursor_.offset() != beforeSpace) {
            hadWhitespace = true;
        }

        const bool atTokenStart = hadWhitespace || !prevWordLike_;
        if (skipCommentIfAny(atTokenStart)) {
            hadWhitespace = true;
            // Comments act like skipped whitespace. If a comment spans lines and
            // closes mid-line, the next loop iteration will refine this flag.
            startOfLine = false;
            continue;
        }
        break;
    }

    if (cursor_.eof()) {
        emittedEOF_ = true;
        Token token = makeToken(TokenKind::Eof, cursor_.offset(), 0);
        token.flags = token.flags | commonFlags;
        if (hadWhitespace) token.flags = token.flags | TokenFlags::LeadingWhitespace;
        if (startOfLine) token.flags = token.flags | TokenFlags::StartOfLine;
        return token;
    }

    if (hadWhitespace) {
        commonFlags = commonFlags | TokenFlags::LeadingWhitespace;
    }
    if (startOfLine) {
        commonFlags = commonFlags | TokenFlags::StartOfLine;
    }

    Token result;
    if (tryScanRawString(result)) {
        // handled below
    } else {
        char32_t cp = 0;
        std::size_t cpLength = 0;
        const bool valid = cursor_.peek(cp, cpLength);
        const SourceLocation start = cursor_.offset();

        if (!valid) {
            cursor_.advance(cpLength == 0 ? 1 : cpLength);
            error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
                  "invalid UTF-8 sequence");
            result = makeToken(TokenKind::Unknown, start,
                               static_cast<std::uint32_t>(cursor_.offset() - start),
                               TokenFlags::Invalid);
        } else if (isIdentifierStart(cp)) {
            result = scanIdentifierOrKeyword(prevWordLike_);
        } else if ((cp >= '0' && cp <= '9') || (cp == '.' && [this]() {
            if (cursor_.offset() + 1 >= cursor_.size()) return false;
            char32_t next = 0;
            std::size_t nextLength = 0;
            cursor_.peek(next, nextLength);
            const std::string_view buffer = sourceManager_.getBuffer(file_);
            char32_t after = 0;
            std::size_t afterLength = 0;
            utf8::decode(buffer.data(), buffer.size(), cursor_.offset() + nextLength, after, afterLength);
            return after >= '0' && after <= '9';
        }())) {
            result = scanNumericConstant();
        } else if (cp == '"') {
            result = scanStringLiteral();
        } else if (cp == '\'') {
            result = scanCharConstant();
        } else {
            result = scanOperatorOrPunctuator();
        }
    }

    result.flags = result.flags | commonFlags;

    const bool wordLike =
        result.kind == TokenKind::Identifier ||
        result.kind == TokenKind::NumericConstant ||
        result.kind == TokenKind::StringLiteral ||
        result.kind == TokenKind::CharConstant ||
        (result.kind >= TokenKind::KwFungsi && result.kind <= TokenKind::KwKosong);
    prevWordLike_ = wordLike;
    return result;
}

void Lexer::skipHorizontalWhitespace() {
    while (!cursor_.eof()) {
        char32_t cp = 0;
        std::size_t length = 0;
        cursor_.peek(cp, length);
        if (!isHorizontalWhitespace(cp)) break;
        cursor_.advance(length == 0 ? 1 : length);
    }
}

bool Lexer::atLogicalNewline() {
    char32_t cp = 0;
    std::size_t length = 0;
    const bool valid = cursor_.peek(cp, length);
    return valid && utf8::isNewline(cp);
}

FileOffset Lexer::consumeLogicalNewline() {
    const SourceLocation start = cursor_.offset();
    char32_t cp = 0;
    std::size_t length = 0;
    const bool valid = cursor_.peek(cp, length);
    if (!valid) {
        cursor_.advance(1);
        return start;
    }
    cursor_.advance(length);
    if (cp == 0x000Du && cursor_.offset() < cursor_.size() &&
        sourceManager_.getBuffer(file_)[cursor_.offset()] == '\n') {
        cursor_.advance(1);
    }
    return start;
}

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

Token Lexer::makeToken(TokenKind kind, SourceLocation localLoc, std::uint32_t length,
                       TokenFlags flags, const IdentifierInfo* ident) {
    Token token;
    token.kind = kind;
    token.flags = flags;
    token.loc = sourceManager_.makeLocation(file_, static_cast<FileOffset>(localLoc));
    token.length = length;
    token.ident = ident;
    return token;
}

Token Lexer::scanIdentifierOrKeyword(bool& wordLike) {
    const SourceLocation start = cursor_.offset();
    bool invalid = false;
    while (!cursor_.eof()) {
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (length == 0) break;
        if (!isIdentifierContinue(cp)) break;
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

Token Lexer::scanNumericConstant() {
    const SourceLocation start = cursor_.offset();
    bool invalid = false;

    if (cursor_.startsWith("0x") || cursor_.startsWith("0X")) {
        cursor_.advance(2);
        bool any = false;
        while (!cursor_.eof()) {
            char32_t cp = 0;
            std::size_t length = 0;
            cursor_.peek(cp, length);
            const bool hex = (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') ||
                             (cp >= 'A' && cp <= 'F');
            if (!hex) break;
            any = true;
            cursor_.advance(length);
        }
        if (!any) {
            invalid = true;
            error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
                  "invalid hexadecimal literal");
        }
    } else {
        while (!cursor_.eof()) {
            char32_t cp = 0;
            std::size_t length = 0;
            cursor_.peek(cp, length);
            if (cp < '0' || cp > '9') break;
            cursor_.advance(length);
        }

        if (!cursor_.eof() && sourceManager_.getBuffer(file_)[cursor_.offset()] == '.') {
            const std::size_t dotOffset = cursor_.offset();
            char32_t after = 0;
            std::size_t afterLength = 0;
            utf8::decode(sourceManager_.getBuffer(file_).data(), cursor_.size(), dotOffset + 1,
                         after, afterLength);
            if (after >= '0' && after <= '9') {
                cursor_.advance(1);
                while (!cursor_.eof()) {
                    char32_t cp = 0;
                    std::size_t length = 0;
                    cursor_.peek(cp, length);
                    if (cp < '0' || cp > '9') break;
                    cursor_.advance(length);
                }
            }
        }

        if (!cursor_.eof()) {
            char32_t cp = 0;
            std::size_t length = 0;
            cursor_.peek(cp, length);
            if (cp == 'e' || cp == 'E') {
                const SourceLocation expLoc = cursor_.offset();
                cursor_.advance(length);
                if (!cursor_.eof()) {
                    char32_t sign = 0;
                    std::size_t signLength = 0;
                    cursor_.peek(sign, signLength);
                    if (sign == '+' || sign == '-') {
                        cursor_.advance(signLength);
                    }
                }
                char32_t digit = 0;
                std::size_t digitLength = 0;
                cursor_.peek(digit, digitLength);
                if (digit < '0' || digit > '9') {
                    invalid = true;
                    error(expLoc, static_cast<std::uint32_t>(cursor_.offset() - expLoc),
                          "invalid exponent in numeric literal");
                } else {
                    while (!cursor_.eof()) {
                        char32_t d = 0;
                        std::size_t dLength = 0;
                        cursor_.peek(d, dLength);
                        if (d < '0' || d > '9') break;
                        cursor_.advance(dLength);
                    }
                }
            }
        }
    }

    if (!cursor_.eof()) {
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (valid && isIdentifierStart(cp)) {
            const SourceLocation suffixLoc = cursor_.offset();
            while (!cursor_.eof()) {
                char32_t next = 0;
                std::size_t nextLength = 0;
                const bool nextValid = cursor_.peek(next, nextLength);
                if (nextLength == 0 || !isIdentifierContinue(next)) break;
                if (!nextValid) {
                    invalid = true;
                    error(cursor_.offset(), 1, "invalid UTF-8 sequence in numeric literal");
                }
                cursor_.advance(nextLength);
            }
            invalid = true;
            error(suffixLoc, static_cast<std::uint32_t>(cursor_.offset() - suffixLoc),
                  "invalid suffix or adjacent identifier in numeric literal");
        }
    }

    TokenFlags flags = invalid ? TokenFlags::Invalid : TokenFlags::None;
    return makeToken(TokenKind::NumericConstant, start,
                     static_cast<std::uint32_t>(cursor_.offset() - start), flags);
}

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

Token Lexer::scanUnknown() {
    const SourceLocation start = cursor_.offset();
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

bool Lexer::isIdentifierStart(char32_t cp) const {
    if (isAsciiIdentifierStart(cp)) return true;
    // U3: broad Unicode identifiers. Semantic narrowing can be added later.
    return cp >= 0x80u && cp != utf8::Replacement && cp != utf8::SentinelEOF;
}

bool Lexer::isIdentifierContinue(char32_t cp) const {
    return isIdentifierStart(cp) || (cp >= '0' && cp <= '9');
}

bool Lexer::isAsciiIdentifierStart(char32_t cp) const {
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
}

bool Lexer::isAsciiIdentifierContinue(char32_t cp) const {
    return isAsciiIdentifierStart(cp) || (cp >= '0' && cp <= '9');
}

bool Lexer::isHorizontalWhitespace(char32_t cp) const {
    return cp == ' ' || cp == '\t';
}

bool Lexer::isValidEscape(char32_t cp) const {
    switch (cp) {
    case 'n':
    case 'r':
    case 't':
    case '\\':
    case '"':
    case '\'':
    case '0':
        return true;
    default:
        return false;
    }
}

void Lexer::error(SourceLocation localLoc, std::uint32_t length, const std::string& message) {
    diagnostics_.error(file_,
                       sourceManager_.makeLocation(file_, static_cast<FileOffset>(localLoc)),
                       length,
                       message);
}

} // namespace rhc
