#pragma once

#include <cstddef>
#include <cstdint>

namespace rhc::utf8 {

inline constexpr char32_t Replacement = 0xFFFD;
inline constexpr char32_t SentinelEOF = 0xFFFFFFFF;

inline bool isContinuationByte(unsigned char c) {
    return (c & 0xC0u) == 0x80u;
}

inline bool isNewline(char32_t cp) {
    return cp == 0x000Au || cp == 0x000Du || cp == 0x0085u ||
           cp == 0x2028u || cp == 0x2029u;
}

inline bool decode(const char* data, std::size_t size, std::size_t pos,
                   char32_t& cp, std::size_t& length) {
    if (pos >= size) {
        cp = SentinelEOF;
        length = 0;
        return true;
    }

    const auto b0 = static_cast<unsigned char>(data[pos]);
    if (b0 < 0x80u) {
        cp = b0;
        length = 1;
        return true;
    }

    auto invalid = [&]() {
        cp = Replacement;
        length = 1;
        return false;
    };

    if (b0 >= 0xC2u && b0 <= 0xDFu) {
        if (pos + 1 >= size) return invalid();
        const auto b1 = static_cast<unsigned char>(data[pos + 1]);
        if (!isContinuationByte(b1)) return invalid();
        cp = static_cast<char32_t>((b0 & 0x1Fu) << 6u) |
             static_cast<char32_t>(b1 & 0x3Fu);
        length = 2;
        return true;
    }

    if (b0 >= 0xE0u && b0 <= 0xEFu) {
        if (pos + 2 >= size) return invalid();
        const auto b1 = static_cast<unsigned char>(data[pos + 1]);
        const auto b2 = static_cast<unsigned char>(data[pos + 2]);
        if (!isContinuationByte(b1) || !isContinuationByte(b2)) return invalid();
        if (b0 == 0xE0u && b1 < 0xA0u) return invalid(); // overlong
        if (b0 == 0xEDu && b1 >= 0xA0u) return invalid(); // surrogate
        cp = static_cast<char32_t>((b0 & 0x0Fu) << 12u) |
             static_cast<char32_t>((b1 & 0x3Fu) << 6u) |
             static_cast<char32_t>(b2 & 0x3Fu);
        length = 3;
        return true;
    }

    if (b0 >= 0xF0u && b0 <= 0xF4u) {
        if (pos + 3 >= size) return invalid();
        const auto b1 = static_cast<unsigned char>(data[pos + 1]);
        const auto b2 = static_cast<unsigned char>(data[pos + 2]);
        const auto b3 = static_cast<unsigned char>(data[pos + 3]);
        if (!isContinuationByte(b1) || !isContinuationByte(b2) || !isContinuationByte(b3)) {
            return invalid();
        }
        if (b0 == 0xF0u && b1 < 0x90u) return invalid(); // overlong
        if (b0 == 0xF4u && b1 >= 0x90u) return invalid(); // > U+10FFFF
        cp = static_cast<char32_t>((b0 & 0x07u) << 18u) |
             static_cast<char32_t>((b1 & 0x3Fu) << 12u) |
             static_cast<char32_t>((b2 & 0x3Fu) << 6u) |
             static_cast<char32_t>(b3 & 0x3Fu);
        length = 4;
        return true;
    }

    return invalid();
}

} // namespace rhc::utf8
