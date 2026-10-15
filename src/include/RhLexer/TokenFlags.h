#pragma once

#include <cstdint>

namespace rhc {

enum class TokenFlags : std::uint16_t {
    None = 0,
    StartOfLine = 1u << 0,
    LeadingWhitespace = 1u << 1,
    RawMode = 1u << 2,
    InDirective = 1u << 3,
    Invalid = 1u << 4,
    EndOfDirective = 1u << 5,
    RawLiteral = 1u << 6,
};

inline TokenFlags operator|(TokenFlags lhs, TokenFlags rhs) {
    return static_cast<TokenFlags>(static_cast<std::uint16_t>(lhs) |
                                   static_cast<std::uint16_t>(rhs));
}

inline TokenFlags operator&(TokenFlags lhs, TokenFlags rhs) {
    return static_cast<TokenFlags>(static_cast<std::uint16_t>(lhs) &
                                   static_cast<std::uint16_t>(rhs));
}

inline bool hasFlag(TokenFlags value, TokenFlags flag) {
    return static_cast<std::uint16_t>(value & flag) != 0;
}

} // namespace rhc
