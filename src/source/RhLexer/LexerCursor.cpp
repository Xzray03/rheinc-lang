#include "RhLexer/LexerCursor.h"

namespace rhc {

bool LexerCursor::peek(char32_t& cp, std::size_t& length) const {
    return utf8::decode(buffer_.data(), buffer_.size(), pos_, cp, length);
}

bool LexerCursor::startsWith(std::string_view text) const {
    return buffer_.substr(pos_, text.size()) == text;
}

void LexerCursor::advance(std::size_t byteCount) {
    pos_ += byteCount;
    if (pos_ > buffer_.size()) {
        pos_ = buffer_.size();
    }
}

void LexerCursor::advanceCodePoint() {
    char32_t cp = 0;
    std::size_t length = 0;
    peek(cp, length);
    if (length == 0) {
        advance(1);
    } else {
        advance(length);
    }
}

} // namespace rhc
