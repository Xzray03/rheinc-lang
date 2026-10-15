#pragma once

#include "RhCore/SourceManager.h"
#include "RhLexer/Token.h"
#include "RhLexer/TokenFlags.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace rhc {
namespace ppdetail {

inline bool isIdentifierNamed(const Token& token, const char* name,
                       const SourceManager& sourceManager) {
    const bool identifierLike =
        token.kind == TokenKind::Identifier ||
        (token.kind >= TokenKind::KwFungsi && token.kind <= TokenKind::KwKosong);
    if (!identifierLike) return false;
    const std::string_view text = sourceManager.getText(
        sourceManager.getFileID(token.loc), token.loc, token.length);
    return text == name;
}

inline void clearPreprocessorOnlyFlags(Token& token) {
    auto value = static_cast<std::uint16_t>(token.flags);
    value &= ~(static_cast<std::uint16_t>(TokenFlags::RawMode) |
               static_cast<std::uint16_t>(TokenFlags::EndOfDirective));
    token.flags = static_cast<TokenFlags>(value);
}

inline bool isPeriodSequence(const std::vector<Token>& tokens, std::size_t pos, std::size_t count,
                      const SourceManager& sourceManager) {
    if (pos + count > tokens.size()) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (tokens[pos + i].kind != TokenKind::Period) return false;
        (void)sourceManager;
    }
    return true;
}

inline long long parseIntegerToken(const Token& token, const SourceManager& sourceManager, bool& ok) {
    const std::string text(sourceManager.getText(
        sourceManager.getFileID(token.loc), token.loc, token.length));
    try {
        std::size_t used = 0;
        const long long value = std::stoll(text, &used, 0);
        ok = used == text.size();
        return value;
    } catch (...) {
        ok = false;
        return 0;
    }
}

inline std::string escapeForStringLiteral(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (char c : input) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

} // namespace ppdetail
} // namespace rhc
