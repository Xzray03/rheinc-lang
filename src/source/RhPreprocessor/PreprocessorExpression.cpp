#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <functional>

namespace rhc {

using namespace ppdetail;

std::vector<Token> Preprocessor::expandForConditional(std::vector<Token> tokens) const {
    bool changed = true;
    int guard = 0;
    while (changed && guard++ < 64) {
        changed = false;
        std::vector<Token> output;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const Token& token = tokens[i];
            if (token.kind == TokenKind::Identifier && token.ident) {
                const std::string_view name = token.ident->getName();
                const bool predicateKeyword =
                    name == "terdefinisi" || name == "tidak" || name == "bukan" ||
                    name == "dan" || name == "atau";

                if (predicateKeyword) {
                    output.push_back(token);
                    continue;
                }

                if (i + 2 < tokens.size() &&
                    tokens[i + 1].kind == TokenKind::Identifier &&
                    tokenText(tokens[i + 1]) == "tidak" &&
                    tokens[i + 2].kind == TokenKind::Identifier &&
                    tokenText(tokens[i + 2]) == "terdefinisi") {
                    Token value;
                    value.kind = TokenKind::NumericConstant;
                    value.loc = token.loc;
                    value.length = 1;
                    value.flags = TokenFlags::None;
                    // Parsed by evaluator through a special marker: undefined handling
                    // happens in parsePrimary, but predicate pairs are already resolved.
                    // To avoid synthetic text, keep the original name and let evaluator
                    // see a simple numeric via a real body-like token from source is not
                    // possible here; instead keep predicate form by rewriting order.
                    // Simpler: preserve tokens and let evaluator handle the pair.
                    output.push_back(token);
                    output.push_back(tokens[i + 1]);
                    output.push_back(tokens[i + 2]);
                    i += 2;
                    continue;
                }

                if (i + 1 < tokens.size() &&
                    tokens[i + 1].kind == TokenKind::Identifier &&
                    tokenText(tokens[i + 1]) == "terdefinisi") {
                    output.push_back(token);
                    output.push_back(tokens[i + 1]);
                    ++i;
                    continue;
                }

                if (const MacroDefinition* macro = macros_.find(name);
                    macro && !macro->isFunctionLike) {
                    output.insert(output.end(), macro->body.begin(), macro->body.end());
                    changed = true;
                    continue;
                }

                // Keep undefined identifiers. The evaluator treats them as zero.
                output.push_back(token);
                continue;
            }
            output.push_back(token);
        }
        tokens = std::move(output);
    }
    return tokens;
}

bool Preprocessor::evaluateConditionalExpression(const std::vector<Token>& tokens, long long& result) {
    std::size_t pos = 0;

    auto peek = [&]() -> const Token* {
        return pos < tokens.size() ? &tokens[pos] : nullptr;
    };
    auto consume = [&]() -> const Token* {
        return pos < tokens.size() ? &tokens[pos++] : nullptr;
    };

    std::function<long long()> parseTernary;
    std::function<long long()> parseOr = [&]() -> long long {
        long long lhs = 0;
        // placeholder, replaced below
        return lhs;
    };

    parseTernary = [&]() -> long long { return 0; };
    (void)parseOr;
    (void)parseTernary;

    // For milestone readability, use a compact recursive descent implementation.
    std::function<long long()> parsePrimary = [&]() -> long long {
        const Token* token = consume();
        if (!token) {
            error(tokens.empty() ? 0 : tokens.back().loc, "unexpected end of #jika expression");
            return 0;
        }
        if (token->kind == TokenKind::LParen) {
            long long value = 0;
            // forward-declared via std::function below
            return value;
        }
        if (token->kind == TokenKind::NumericConstant || token->kind == TokenKind::CharConstant) {
            bool ok = false;
            const long long value = parseIntegerToken(*token, sourceManager_, ok);
            if (!ok && token->kind == TokenKind::NumericConstant) {
                // tokenText is unavailable for synthetic zero tokens; they have loc in source.
                error(token->loc, "invalid integer constant in #jika");
            }
            return value;
        }
        if (token->kind == TokenKind::Identifier) {
            if (peek() && isIdentifierNamed(*peek(), "terdefinisi", sourceManager_)) {
                consume();
                return macros_.isDefined(tokenText(*token)) ? 1 : 0;
            }
            if (pos + 1 < tokens.size() &&
                isIdentifierNamed(tokens[pos], "tidak", sourceManager_) &&
                isIdentifierNamed(tokens[pos + 1], "terdefinisi", sourceManager_)) {
                consume();
                consume();
                return macros_.isDefined(tokenText(*token)) ? 0 : 1;
            }
            if (isIdentifierNamed(*token, "terdefinisi", sourceManager_)) {
                const Token* name = consume();
                if (!name || name->kind != TokenKind::Identifier) {
                    error(token->loc, "terdefinisi expects a macro name");
                    return 0;
                }
                return macros_.isDefined(tokenText(*name)) ? 1 : 0;
            }
            if (isIdentifierNamed(*token, "bukan", sourceManager_)) {
                return parsePrimary() == 0 ? 1 : 0;
            }
            // Undefined identifiers were already replaced by zero in expandForConditional.
            return 0;
        }
        error(token->loc, "unexpected token in #jika expression");
        return 0;
    };

    // Rewrite parser with explicit precedence functions.
    std::function<long long()> parseUnary;
    std::function<long long()> parseMul;
    std::function<long long()> parseAdd;
    std::function<long long()> parseShift;
    std::function<long long()> parseRel;
    std::function<long long()> parseEq;
    std::function<long long()> parseBitAnd;
    std::function<long long()> parseBitXor;
    std::function<long long()> parseBitOr;
    std::function<long long()> parseLogicalAnd;
    std::function<long long()> parseLogicalOr;
    std::function<long long()> parseTernaryImpl;
    std::function<long long()> parseExpression;

    parsePrimary = [&]() -> long long {
        const Token* token = consume();
        if (!token) {
            error(tokens.empty() ? 0 : tokens.back().loc, "unexpected end of #jika expression");
            return 0;
        }
        if (token->kind == TokenKind::LParen) {
            const long long value = parseExpression();
            const Token* close = consume();
            if (!close || close->kind != TokenKind::RParen) {
                error(token->loc, "expected ) in #jika expression");
            }
            return value;
        }
        if (token->kind == TokenKind::NumericConstant || token->kind == TokenKind::CharConstant) {
            bool ok = false;
            const long long value = parseIntegerToken(*token, sourceManager_, ok);
            if (!ok) {
                std::string text = tokenText(*token);
                if (text.size() == 1) return static_cast<unsigned char>(text[0]);
                error(token->loc, "invalid integer constant in #jika");
            }
            return value;
        }
        if (token->kind == TokenKind::Identifier) {
            if (peek() && isIdentifierNamed(*peek(), "terdefinisi", sourceManager_)) {
                consume();
                return macros_.isDefined(tokenText(*token)) ? 1 : 0;
            }
            if (pos + 1 < tokens.size() &&
                isIdentifierNamed(tokens[pos], "tidak", sourceManager_) &&
                isIdentifierNamed(tokens[pos + 1], "terdefinisi", sourceManager_)) {
                consume();
                consume();
                return macros_.isDefined(tokenText(*token)) ? 0 : 1;
            }
            if (isIdentifierNamed(*token, "terdefinisi", sourceManager_)) {
                const Token* name = consume();
                if (!name || name->kind != TokenKind::Identifier) {
                    error(token->loc, "terdefinisi expects a macro name");
                    return 0;
                }
                return macros_.isDefined(tokenText(*name)) ? 1 : 0;
            }
            if (isIdentifierNamed(*token, "tidak", sourceManager_) &&
                peek() && isIdentifierNamed(*peek(), "terdefinisi", sourceManager_)) {
                consume();
                const Token* name = consume();
                if (!name || name->kind != TokenKind::Identifier) {
                    error(token->loc, "tidak terdefinisi expects a macro name");
                    return 0;
                }
                return macros_.isDefined(tokenText(*name)) ? 0 : 1;
            }
            if (isIdentifierNamed(*token, "bukan", sourceManager_)) {
                return parseUnary() == 0 ? 1 : 0;
            }
            return 0;
        }
        error(token->loc, "unexpected token in #jika expression");
        return 0;
    };

    parseUnary = [&]() -> long long {
        const Token* token = peek();
        if (!token) return parsePrimary();
        if (token->kind == TokenKind::Minus) {
            consume();
            return -parseUnary();
        }
        if (token->kind == TokenKind::Plus) {
            consume();
            return parseUnary();
        }
        if (token->kind == TokenKind::Tilde) {
            consume();
            return ~parseUnary();
        }
        return parsePrimary();
    };

    auto binaryLeft = [&](std::function<long long()> next, std::initializer_list<TokenKind> ops) -> long long {
        long long lhs = next();
        while (const Token* op = peek()) {
            bool match = false;
            for (TokenKind kind : ops) if (op->kind == kind) match = true;
            if (!match) break;
            consume();
            const long long rhs = next();
            switch (op->kind) {
            case TokenKind::Star: lhs *= rhs; break;
            case TokenKind::Slash: lhs = rhs == 0 ? 0 : lhs / rhs; break;
            case TokenKind::SlashSlash: lhs = rhs == 0 ? 0 : lhs / rhs; break;
            case TokenKind::Percent: lhs = rhs == 0 ? 0 : lhs % rhs; break;
            case TokenKind::Plus: lhs += rhs; break;
            case TokenKind::Minus: lhs -= rhs; break;
            case TokenKind::LessLess: lhs <<= rhs; break;
            case TokenKind::GreaterGreater: lhs >>= rhs; break;
            case TokenKind::Less: lhs = lhs < rhs; break;
            case TokenKind::Greater: lhs = lhs > rhs; break;
            case TokenKind::LessEqual: lhs = lhs <= rhs; break;
            case TokenKind::GreaterEqual: lhs = lhs >= rhs; break;
            case TokenKind::EqualEqual: lhs = lhs == rhs; break;
            case TokenKind::BangEqual: lhs = lhs != rhs; break;
            case TokenKind::Amp: lhs &= rhs; break;
            case TokenKind::Caret: lhs ^= rhs; break;
            case TokenKind::Pipe: lhs |= rhs; break;
            case TokenKind::StarStar: {
                long long out = 1;
                for (long long i = 0; i < rhs; ++i) out *= lhs;
                lhs = out;
                break;
            }
            default: break;
            }
        }
        return lhs;
    };

    auto logicalNext = [&](std::function<long long()> next, TokenKind keyword,
                           const char* spelling) -> std::function<long long()> {
        return [next, keyword, spelling, this, &peek, &consume]() -> long long {
            long long lhs = next();
            while (const Token* op = peek()) {
                const bool matches =
                    (keyword == TokenKind::Amp && op->kind == TokenKind::KwDan) ||
                    (keyword == TokenKind::Pipe && op->kind == TokenKind::KwAtau) ||
                    tokenText(*op) == spelling;
                if (!matches) break;
                consume();
                const long long rhs = next();
                lhs = keyword == TokenKind::Amp ? (lhs != 0 && rhs != 0)
                                                : (lhs != 0 || rhs != 0);
            }
            return lhs;
        };
    };

    parseMul = [&]() { return binaryLeft(parseUnary, {TokenKind::Star, TokenKind::Slash, TokenKind::SlashSlash, TokenKind::Percent, TokenKind::StarStar}); };
    parseAdd = [&]() { return binaryLeft(parseMul, {TokenKind::Plus, TokenKind::Minus}); };
    parseShift = [&]() { return binaryLeft(parseAdd, {TokenKind::LessLess, TokenKind::GreaterGreater}); };
    parseRel = [&]() { return binaryLeft(parseShift, {TokenKind::Less, TokenKind::Greater, TokenKind::LessEqual, TokenKind::GreaterEqual}); };
    parseEq = [&]() { return binaryLeft(parseRel, {TokenKind::EqualEqual, TokenKind::BangEqual}); };
    parseBitAnd = [&]() { return binaryLeft(parseEq, {TokenKind::Amp}); };
    parseBitXor = [&]() { return binaryLeft(parseBitAnd, {TokenKind::Caret}); };
    parseBitOr = [&]() { return binaryLeft(parseBitXor, {TokenKind::Pipe}); };
    parseLogicalAnd = logicalNext(parseBitOr, TokenKind::Amp, "dan");
    parseLogicalOr = logicalNext(parseLogicalAnd, TokenKind::Pipe, "atau");
    parseTernaryImpl = [&]() -> long long {
        const long long condition = parseLogicalOr();
        if (const Token* question = peek(); question && question->kind == TokenKind::Question) {
            consume();
            const long long trueValue = parseExpression();
            const Token* colon = consume();
            if (!colon || colon->kind != TokenKind::Colon) {
                error(question->loc, "expected : in #jika ternary expression");
                return condition;
            }
            const long long falseValue = parseTernaryImpl();
            return condition != 0 ? trueValue : falseValue;
        }
        return condition;
    };
    parseExpression = parseTernaryImpl;

    result = parseExpression();
    if (pos != tokens.size()) {
        error(tokens[pos].loc, "unexpected trailing token in #jika expression");
        return false;
    }
    return true;
}


} // namespace rhc
