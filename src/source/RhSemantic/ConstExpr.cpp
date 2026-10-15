#include "RhSemantic/ConstExpr.h"

#include <cctype>

namespace rhc {

std::optional<std::int64_t> parseIntegerLiteral(const std::string& raw) {
    std::string text;
    text.reserve(raw.size());
    for (char c : raw) {
        if (c != '_') text.push_back(c);
    }
    if (text.empty()) return std::nullopt;
    try {
        std::size_t pos = 0;
        int base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            pos = 2;
            if (pos == text.size()) return std::nullopt;
        } else if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
            base = 2;
            pos = 2;
            if (pos == text.size()) return std::nullopt;
        }
        for (std::size_t i = pos; i < text.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            if (!std::isalnum(c)) return std::nullopt;
        }
        return std::stoll(text.substr(pos), nullptr, base);
    } catch (...) {
        return std::nullopt;
    }
}

static std::optional<std::int64_t> charValue(const std::string& text) {
    if (text.size() == 1) return static_cast<std::int64_t>(text[0]);
    if (text.size() == 2 && text[0] == '\\') {
        switch (text[1]) {
            case 'n': return std::int64_t{'\n'};
            case 't': return std::int64_t{'\t'};
            case 'r': return std::int64_t{'\r'};
            case '0': return 0;
            case '\\': return std::int64_t{'\\'};
            case '\'': return std::int64_t{'\''};
            case '"': return std::int64_t{'"'};
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t> evaluateConstInt(ExprNode* expr,
                                             const ConstTable& constants) {
    if (!expr) return std::nullopt;
    switch (expr->kind) {
        case AstKind::IntegerLiteralExpr:
            return parseIntegerLiteral(static_cast<IntegerLiteralExpr*>(expr)->text);
        case AstKind::CharLiteralExpr:
            return charValue(static_cast<CharLiteralExpr*>(expr)->text);
        case AstKind::IdentifierExpr: {
            const std::string& name = static_cast<IdentifierExpr*>(expr)->name;
            auto it = constants.find(name);
            if (it != constants.end()) return it->second;
            return std::nullopt;
        }
        case AstKind::UnaryExpr: {
            auto* unary = static_cast<UnaryExpr*>(expr);
            auto operand = evaluateConstInt(unary->operand, constants);
            if (!operand) return std::nullopt;
            switch (unary->op) {
                case TokenKind::Minus: return -*operand;
                case TokenKind::Plus: return *operand;
                case TokenKind::Tilde: return ~(*operand);
                default: return std::nullopt;
            }
        }
        case AstKind::BinaryExpr: {
            auto* binary = static_cast<BinaryExpr*>(expr);
            auto lhs = evaluateConstInt(binary->lhs, constants);
            auto rhs = evaluateConstInt(binary->rhs, constants);
            if (!lhs || !rhs) return std::nullopt;
            switch (binary->op) {
                case TokenKind::Plus: return *lhs + *rhs;
                case TokenKind::Minus: return *lhs - *rhs;
                case TokenKind::Star: return *lhs * *rhs;
                case TokenKind::Slash:
                case TokenKind::SlashSlash:
                    if (*rhs == 0) return std::nullopt;
                    return *lhs / *rhs;
                case TokenKind::Percent:
                    if (*rhs == 0) return std::nullopt;
                    return *lhs % *rhs;
                case TokenKind::Amp: return *lhs & *rhs;
                case TokenKind::Pipe: return *lhs | *rhs;
                case TokenKind::Caret: return *lhs ^ *rhs;
                case TokenKind::LessLess:
                    if (*rhs < 0 || *rhs >= 64) return std::nullopt;
                    return *lhs << *rhs;
                case TokenKind::GreaterGreater:
                    if (*rhs < 0 || *rhs >= 64) return std::nullopt;
                    return *lhs >> *rhs;
                default: return std::nullopt;
            }
        }
        default:
            return std::nullopt;
    }
}

} // namespace rhc
