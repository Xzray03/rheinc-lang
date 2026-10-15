#include "RhParser/ParserTokenStream.h"

namespace rhc {

ParserTokenStream::ParserTokenStream(Preprocessor& preprocessor,
                                     DiagnosticEngine& diagnostics)
    : preprocessor_(preprocessor), diagnostics_(diagnostics) {}

const Token& ParserTokenStream::current() {
    fill(1);
    return buffer_.front();
}

const Token& ParserTokenStream::peek(std::size_t n) {
    fill(n + 1);
    return buffer_[n];
}

Token ParserTokenStream::consume() {
    fill(1);
    Token result = buffer_.front();
    buffer_.pop_front();
    fill(1);
    return result;
}

bool ParserTokenStream::is(TokenKind kind) const {
    return !buffer_.empty() && buffer_.front().kind == kind;
}

bool ParserTokenStream::isContextual(ContextualKeywordID id) const {
    return !buffer_.empty() && buffer_.front().kind == TokenKind::Identifier &&
           buffer_.front().ident && buffer_.front().ident->getContextualKeywordID() == id;
}

bool ParserTokenStream::tryConsume(TokenKind kind) {
    if (!is(kind)) return false;
    consume();
    return true;
}

bool ParserTokenStream::tryConsumeContextual(ContextualKeywordID id) {
    if (!isContextual(id)) return false;
    consume();
    return true;
}

bool ParserTokenStream::expect(TokenKind kind, const char* what) {
    if (tryConsume(kind)) return true;
    const SourceLocation loc = buffer_.empty() ? 0 : buffer_.front().loc;
    diagnostics_.error(FileID(), loc, 0, std::string("expected ") + what);
    return false;
}

void ParserTokenStream::fill(std::size_t count) {
    while (buffer_.size() < count) {
        Token token;
        if (!preprocessor_.lex(token)) {
            token.kind = TokenKind::Eof;
            token.loc = 0;
            token.length = 0;
            token.ident = nullptr;
            token.flags = TokenFlags::None;
        }
        buffer_.push_back(token);
        if (token.kind == TokenKind::Eof) break;
    }
}

} // namespace rhc
