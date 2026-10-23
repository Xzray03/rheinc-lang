#include "RhLexer/Lexer.h"

#include "RhCore/Utf8.h"
#include "RhLexer/LexerCharClass.h"

namespace rhc {

namespace {

// Consumes a run of digits. A single '_' is accepted as a digit separator only
// when it sits directly between two digits ("1_000"); any other '_' is left in
// the buffer so the suffix check below reports it. Returns true if at least one
// digit was consumed.
template <typename IsDigit>
bool consumeDigitRun(LexerCursor& cursor, std::string_view buffer, IsDigit isDigit) {
        bool any = false;
        while (!cursor.eof()) {
            char32_t cp = 0;
            std::size_t length = 0;
            cursor.peek(cp, length);
            if (isDigit(cp)) {
                any = true;
                cursor.advance(length);
                continue;
            }
            if (any && cp == '_') {
                char32_t next = 0;
                std::size_t nextLength = 0;
                utf8::decode(buffer.data(), buffer.size(), cursor.offset() + length,
                             next, nextLength);
                if (isDigit(next)) {
                    cursor.advance(length);
                    continue;
                }
            }
            break;
        }
        return any;
}

} // namespace


Token Lexer::scanNumericConstant() {
    const SourceLocation start = cursor_.offset();
    bool invalid = false;

    const auto isDecDigit = [](char32_t cp) { return cp >= '0' && cp <= '9'; };
    const auto isBinDigit = [](char32_t cp) { return cp == '0' || cp == '1'; };
    const auto isHexDigit = [](char32_t cp) {
        return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'f') ||
               (cp >= 'A' && cp <= 'F');
    };


    if (cursor_.startsWith("0x") || cursor_.startsWith("0X")) {
        cursor_.advance(2);
        const bool any = consumeDigitRun(cursor_, sourceManager_.getBuffer(file_), isHexDigit);
        if (!any) {
            invalid = true;
            error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
                  "invalid hexadecimal literal");
        }
    } else if (cursor_.startsWith("0b") || cursor_.startsWith("0B")) {
        cursor_.advance(2);
        const bool any = consumeDigitRun(cursor_, sourceManager_.getBuffer(file_), isBinDigit);
        if (!any) {
            invalid = true;
            error(start, static_cast<std::uint32_t>(cursor_.offset() - start),
                  "invalid binary literal");
        }
        if (!cursor_.eof()) {
            // A decimal digit right after the binary digits ("0b102") is not a new
            // token; swallow the rest of the word and report it as one bad literal.
            char32_t cp = 0;
            std::size_t length = 0;
            cursor_.peek(cp, length);
            if (cp >= '2' && cp <= '9') {
                const SourceLocation digitLoc = cursor_.offset();
                while (!cursor_.eof()) {
                    char32_t next = 0;
                    std::size_t nextLength = 0;
                    const bool nextValid = cursor_.peek(next, nextLength);
                    if (nextLength == 0 || !nextValid || !isIdentifierContinue(next)) break;
                    cursor_.advance(nextLength);
                }
                invalid = true;
                error(digitLoc, static_cast<std::uint32_t>(cursor_.offset() - digitLoc),
                      "invalid digit in binary literal");
            }
        }
    } else {
        consumeDigitRun(cursor_, sourceManager_.getBuffer(file_), isDecDigit);

        if (!cursor_.eof() && sourceManager_.getBuffer(file_)[cursor_.offset()] == '.') {
            const std::size_t dotOffset = cursor_.offset();
            char32_t after = 0;
            std::size_t afterLength = 0;
            utf8::decode(sourceManager_.getBuffer(file_).data(), cursor_.size(), dotOffset + 1,
                         after, afterLength);
            if (after >= '0' && after <= '9') {
                cursor_.advance(1);
                consumeDigitRun(cursor_, sourceManager_.getBuffer(file_), isDecDigit);
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
                    consumeDigitRun(cursor_, sourceManager_.getBuffer(file_), isDecDigit);
                }
            }
        }
    }

    if (!cursor_.eof()) {
        char32_t cp = 0;
        std::size_t length = 0;
        const bool valid = cursor_.peek(cp, length);
        if (valid && isIdentifierStart(cp)) {
            // If the literal body was already diagnosed (e.g. "0x_FF"), the trailing
            // word is still swallowed into this token but not reported a second time.
            const bool alreadyReported = invalid;
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
            const std::uint32_t suffixLength =
                static_cast<std::uint32_t>(cursor_.offset() - suffixLoc);
            const std::string_view suffix = sourceManager_.getText(file_, suffixLoc, suffixLength);
            bool separatorOnly = !suffix.empty() && suffix.front() == '_';
            for (const char c : suffix) {
                if (c != '_' && !(c >= '0' && c <= '9')) {
                    separatorOnly = false;
                    break;
                }
            }
            if (!alreadyReported) {
                error(suffixLoc, suffixLength,
                      separatorOnly
                          ? "invalid digit separator in numeric literal"
                          : "invalid suffix or adjacent identifier in numeric literal");
            }
        }
    }

    TokenFlags flags = invalid ? TokenFlags::Invalid : TokenFlags::None;
    return makeToken(TokenKind::NumericConstant, start,
                     static_cast<std::uint32_t>(cursor_.offset() - start), flags);
}

} // namespace rhc
