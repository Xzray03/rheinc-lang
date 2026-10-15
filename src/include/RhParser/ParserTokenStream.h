#pragma once

#include "RhCore/DiagnosticEngine.h"
#include "RhLexer/Token.h"
#include "RhPreprocessor/Preprocessor.h"

#include <deque>

namespace rhc {

class ParserTokenStream {
public:
    ParserTokenStream(Preprocessor& preprocessor, DiagnosticEngine& diagnostics);

    const Token& current();
    const Token& peek(std::size_t n = 1);

    Token consume();
    bool is(TokenKind kind) const;
    bool isContextual(ContextualKeywordID id) const;
    bool tryConsume(TokenKind kind);
    bool tryConsumeContextual(ContextualKeywordID id);
    bool expect(TokenKind kind, const char* what);

private:
    void fill(std::size_t count);

    Preprocessor& preprocessor_;
    DiagnosticEngine& diagnostics_;
    std::deque<Token> buffer_;
};

} // namespace rhc
