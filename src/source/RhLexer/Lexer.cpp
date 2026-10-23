// Lexer.cpp - lexing driver untuk source Rheinc.
// Tanggung jawab file ini: konstruksi + orkestrasi tokenisasi (lexImpl).
// Detail scanning per kategori token ada di folder Lexer/ (1 file 1 tanggung
// jawab); klasifikasi karakter inline ada di RhLexer/LexerCharClass.h.
#include "RhLexer/Lexer.h"

#include "RhCore/Utf8.h"
#include "RhLexer/LexerCharClass.h"

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
    bool startOfLine = cursor_.offset() == 0 || atLineStart_;
    atLineStart_ = false;

    while (true) {
        if (rawMode && atLogicalNewline()) {
            const SourceLocation start = cursor_.offset();
            consumeLogicalNewline();
            Token token = makeToken(TokenKind::EndOfDirective, start,
                                    static_cast<std::uint32_t>(cursor_.offset() - start),
                                    TokenFlags::EndOfDirective);
            token.flags = token.flags | commonFlags;
            prevWordLike_ = false;
            atLineStart_ = true;
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

} // namespace rhc
