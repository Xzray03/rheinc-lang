#pragma once

#include "RhCore/Utf8.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rhc {

class LexerCursor {
public:
    LexerCursor(std::string_view buffer) : buffer_(buffer) {}

    std::size_t offset() const { return pos_; }
    std::size_t size() const { return buffer_.size(); }
    bool eof() const { return pos_ >= buffer_.size(); }

    bool peek(char32_t& cp, std::size_t& length) const;
    bool startsWith(std::string_view text) const;
    void advance(std::size_t byteCount = 1);
    void advanceCodePoint();

private:
    std::string_view buffer_;
    std::size_t pos_ = 0;
};

} // namespace rhc
