#pragma once

#include "RhLexer/TokenKind.h"

#include <cstdint>
#include <string_view>

namespace rhc {

enum class ContextualKeywordID : std::uint16_t {
    None,

    Dengan,
    Ke,
    Langkah,
    Setiap,
    Dalam,
    Bawaan,

    Selain,
    Itu,

    Bukan,
    Semua,
    Diatas,

    Saat,
    Keluar,

    Coroutine,
    Pembangkit,

    Raw,
};

class IdentifierInfo {
public:
    IdentifierInfo(std::string_view name, TokenKind reservedKind,
                   ContextualKeywordID contextualID, std::uint32_t hash)
        : name_(name), reservedKind_(reservedKind), contextualID_(contextualID), hash_(hash) {}

    std::string_view getName() const { return name_; }
    std::uint32_t getHash() const { return hash_; }

    bool isReservedKeyword() const { return reservedKind_ != TokenKind::Identifier; }
    TokenKind getReservedKeywordKind() const { return reservedKind_; }

    bool isContextualKeyword() const { return contextualID_ != ContextualKeywordID::None; }
    ContextualKeywordID getContextualKeywordID() const { return contextualID_; }

private:
    std::string_view name_;
    TokenKind reservedKind_ = TokenKind::Identifier;
    ContextualKeywordID contextualID_ = ContextualKeywordID::None;
    std::uint32_t hash_ = 0;
};

} // namespace rhc
