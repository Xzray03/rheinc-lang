#pragma once

#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhLexer/LexerCursor.h"
#include "RhLexer/Token.h"

namespace rhc {

class Lexer {
public:
    Lexer(FileID file, const SourceManager& sourceManager,
          IdentifierTable& identifierTable, DiagnosticEngine& diagnostics,
          const LangOptions& options);

    Token lex();
    Token lexRaw();

private:
    Token lexImpl(bool rawMode);

    void skipHorizontalWhitespace();
    bool atLogicalNewline();
    FileOffset consumeLogicalNewline();

    bool skipCommentIfAny(bool atTokenStart);
    bool trySkipLineComment();
    bool trySkipBlockComment();

    Token makeToken(TokenKind kind, SourceLocation loc, std::uint32_t length,
                    TokenFlags flags = TokenFlags::None,
                    const IdentifierInfo* ident = nullptr);

    Token scanIdentifierOrKeyword(bool& wordLike);
    Token scanNumericConstant();
    Token scanStringLiteral();
    Token scanCharConstant();
    bool tryScanRawString(Token& result);
    Token scanOperatorOrPunctuator();
    Token scanUnknown();

    bool isIdentifierStart(char32_t cp) const;
    bool isIdentifierContinue(char32_t cp) const;
    bool isAsciiIdentifierStart(char32_t cp) const;
    bool isAsciiIdentifierContinue(char32_t cp) const;

    bool isHorizontalWhitespace(char32_t cp) const;
    bool isValidEscape(char32_t cp) const;

    void error(SourceLocation loc, std::uint32_t length, const std::string& message);

    FileID file_;
    const SourceManager& sourceManager_;
    IdentifierTable& identifierTable_;
    DiagnosticEngine& diagnostics_;
    LangOptions options_;
    LexerCursor cursor_;
    bool emittedEOF_ = false;
    bool prevWordLike_ = false;
    // Mode raw mengembalikan newline sebagai token EndOfDirective lalu langsung return,
    // sehingga status "awal baris" harus dibawa ke pemanggilan lexImpl berikutnya.
    bool atLineStart_ = false;
};

} // namespace rhc
