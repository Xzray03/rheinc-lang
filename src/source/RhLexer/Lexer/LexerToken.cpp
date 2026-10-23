#include "RhLexer/Lexer.h"

namespace rhc {


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


void Lexer::error(SourceLocation localLoc, std::uint32_t length, const std::string& message) {
    diagnostics_.error(file_,
                       sourceManager_.makeLocation(file_, static_cast<FileOffset>(localLoc)),
                       length,
                       message);
}

} // namespace rhc
