#include "RhLexer/Lexer.h"

#include "RhCore/Utf8.h"
namespace rhc {


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

} // namespace rhc
