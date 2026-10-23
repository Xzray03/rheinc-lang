#include "RhLexer/Lexer.h"

#include "RhLexer/LexerCharClass.h"
namespace rhc {


void Lexer::skipHorizontalWhitespace() {
    while (!cursor_.eof()) {
        char32_t cp = 0;
        std::size_t length = 0;
        cursor_.peek(cp, length);
        if (!isHorizontalWhitespace(cp)) break;
        cursor_.advance(length == 0 ? 1 : length);
    }
}

} // namespace rhc
