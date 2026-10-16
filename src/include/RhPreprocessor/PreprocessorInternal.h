#pragma once

#include "RhCore/SourceManager.h"
#include "RhLexer/Token.h"
#include "RhLexer/TokenFlags.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    const std::string_view raw = sourceManager.getText(
        sourceManager.getFileID(token.loc), token.loc, token.length);
    // '_' is a digit separator in numeric literals; it never affects the value.
    std::string text;
    text.reserve(raw.size());
    for (const char c : raw) {
        if (c != '_') text.push_back(c);
    }
    try {
        std::size_t used = 0;
        std::size_t prefix = 0;
        int base = 0;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
            prefix = 2;
            base = 2;
        }
        const long long value = std::stoll(text.substr(prefix), &used, base);
        ok = used == text.size() - prefix;
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


// Collects macro arguments after the opening '(' until the matching ')'. `next`
// supplies tokens one by one and returns false when the input is exhausted;
// `terminated` reports whether the closing ')' was found. Commas nested inside
// parentheses stay inside their argument.
template <typename NextToken>
inline std::vector<std::vector<Token>> collectMacroArguments(NextToken next, bool& terminated) {
    std::vector<std::vector<Token>> args;
    std::vector<Token> current;
    int depth = 0;
    terminated = false;

    while (true) {
        Token token;
        if (!next(token)) break;
        if (token.kind == TokenKind::EndOfDirective) continue;
        if (token.kind == TokenKind::LParen) {
            ++depth;
            current.push_back(token);
            continue;
        }
        if (token.kind == TokenKind::RParen) {
            if (depth == 0) {
                if (!current.empty() || args.empty()) args.push_back(std::move(current));
                terminated = true;
                break;
            }
            --depth;
            current.push_back(token);
            continue;
        }
        if (token.kind == TokenKind::Comma && depth == 0) {
            args.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(token);
    }
    return args;
}

} // namespace ppdetail
} // namespace rhc
