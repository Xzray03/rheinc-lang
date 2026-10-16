#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <cstddef>
#include <utility>

namespace rhc {

using namespace ppdetail;

namespace {

// Collects macro arguments after the opening '(' until the matching ')'. `next`
// supplies tokens one by one and returns false when the input is exhausted;
// `terminated` reports whether the closing ')' was found. Commas nested inside
// parentheses stay inside their argument.
template <typename NextToken>
std::vector<std::vector<Token>> collectMacroArguments(NextToken next, bool& terminated) {
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

} // namespace

bool Preprocessor::expandIfNeeded(const Token& token, std::vector<Token>& output) {
    if (token.kind != TokenKind::Identifier || !token.ident) return false;
    const MacroDefinition* macro = macros_.find(token.ident->getName());
    if (!macro) return false;

    std::unordered_set<std::string> hidden;
    if (macro->isFunctionLike) {
        Token next;
        if (!peekRawToken(next) || next.kind != TokenKind::LParen) return false;
        return invokeFunctionLikeMacro(*macro, token.loc, std::move(hidden), output);
    }
    return invokeObjectLikeMacro(*macro, token.loc, std::move(hidden), output);
}

std::vector<Token> Preprocessor::expandTokenSequence(
    std::vector<Token> tokens, std::unordered_set<std::string> hidden) {
    std::vector<Token> output;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];
        if (token.kind == TokenKind::Identifier && token.ident) {
            const std::string name(token.ident->getName());
            if (hidden.contains(name)) {
                output.push_back(token);
                continue;
            }
            const MacroDefinition* macro = macros_.find(name);
            if (!macro) {
                output.push_back(token);
                continue;
            }
            if (macro->isFunctionLike) {
                // A function-like macro is only invoked when '(' follows its name.
                if (i + 1 >= tokens.size() || tokens[i + 1].kind != TokenKind::LParen) {
                    output.push_back(token);
                    continue;
                }
                std::size_t pos = i + 2;
                bool terminated = false;
                std::vector<std::vector<Token>> args = collectMacroArguments(
                    [&](Token& out) {
                        if (pos >= tokens.size()) return false;
                        out = tokens[pos++];
                        return true;
                    },
                    terminated);
                if (!terminated) {
                    output.push_back(token);
                    continue;
                }
                std::vector<Token> nested;
                invokeFunctionLikeMacroWithArgs(*macro, token.loc, hidden, std::move(args),
                                                nested);
                output.insert(output.end(), nested.begin(), nested.end());
                i = pos - 1;
                continue;
            }
            std::vector<Token> nested;
            if (invokeObjectLikeMacro(*macro, token.loc, hidden, nested)) {
                output.insert(output.end(), nested.begin(), nested.end());
                continue;
            }
        }
        output.push_back(token);
    }
    return output;
}

bool Preprocessor::invokeObjectLikeMacro(const MacroDefinition& macro,
                                         SourceLocation macroNameLoc,
                                         std::unordered_set<std::string> hidden,
                                         std::vector<Token>& output) {
    hidden.insert(macro.name);
    std::vector<Token> mapped = remapToExpansion(macro.body, macroNameLoc);
    output = expandTokenSequence(std::move(mapped), std::move(hidden));
    return true;
}

bool Preprocessor::invokeFunctionLikeMacro(const MacroDefinition& macro,
                                           SourceLocation macroNameLoc,
                                           std::unordered_set<std::string> hidden,
                                           std::vector<Token>& output) {
    std::vector<std::vector<Token>> args = parseMacroArguments();
    return invokeFunctionLikeMacroWithArgs(macro, macroNameLoc, std::move(hidden),
                                           std::move(args), output);
}

bool Preprocessor::invokeFunctionLikeMacroWithArgs(const MacroDefinition& macro,
                                                   SourceLocation macroNameLoc,
                                                   std::unordered_set<std::string> hidden,
                                                   std::vector<std::vector<Token>> args,
                                                   std::vector<Token>& output) {
    if (macro.isVariadic && !macro.parameters.empty()) {
        // The last parameter is __VA_ARGS__: it takes every remaining argument
        // (comma-separated again) and is empty when none were supplied.
        const std::size_t fixed = macro.parameters.size() - 1;
        std::vector<Token> variadic;
        for (std::size_t i = fixed; i < args.size(); ++i) {
            if (i > fixed) variadic.push_back(synthesizeToken(",", macroNameLoc));
            variadic.insert(variadic.end(), args[i].begin(), args[i].end());
        }
        if (args.size() > fixed) args.resize(fixed);
        while (args.size() < fixed) args.emplace_back();
        args.push_back(std::move(variadic));
    }
    // Arguments are fully expanded before substitution, so the macro's own name is
    // not hidden yet: MAX(MAX(1, 2), 3) must expand the inner MAX as well.
    std::vector<Token> substituted = substituteMacroBody(macro, args, hidden);
    std::vector<Token> pasted = applyTokenPaste(substituted, macroNameLoc);
    std::vector<Token> mapped = remapToExpansion(pasted, macroNameLoc);
    // While the replacement list is rescanned the macro's own name is hidden, which
    // is what stops recursive definitions.
    hidden.insert(macro.name);
    output = expandTokenSequence(std::move(mapped), std::move(hidden));
    return true;
}

std::vector<std::vector<Token>> Preprocessor::parseMacroArguments() {
    Token open;
    getRawToken(open); // '('
    if (open.kind != TokenKind::LParen) return {};

    bool terminated = false;
    return collectMacroArguments([this](Token& token) { return getRawToken(token); },
                                 terminated);
}

std::vector<Token> Preprocessor::substituteMacroBody(
    const MacroDefinition& macro, const std::vector<std::vector<Token>>& args,
    const std::unordered_set<std::string>& hidden) {
    std::unordered_map<std::string, int> parameterIndex;
    for (std::size_t i = 0; i < macro.parameters.size(); ++i) {
        parameterIndex.emplace(macro.parameters[i], static_cast<int>(i));
    }

    auto getArg = [&](std::string_view name) -> const std::vector<Token>* {
        const auto it = parameterIndex.find(std::string(name));
        if (it == parameterIndex.end()) return nullptr;
        if (static_cast<std::size_t>(it->second) >= args.size()) return nullptr;
        return &args[static_cast<std::size_t>(it->second)];
    };

    // __VA_OPT__(tokens) expands to its tokens only when the variadic argument is
    // non-empty, and to nothing otherwise.
    bool hasVariadicArgs = false;
    if (macro.isVariadic && !macro.parameters.empty()) {
        const std::size_t variadicIndex = macro.parameters.size() - 1;
        if (variadicIndex < args.size()) {
            for (const Token& argToken : args[variadicIndex]) {
                if (argToken.kind != TokenKind::EndOfDirective) {
                    hasVariadicArgs = true;
                    break;
                }
            }
        }
    }

    std::vector<Token> body;
    body.reserve(macro.body.size());
    for (std::size_t i = 0; i < macro.body.size(); ++i) {
        const Token& token = macro.body[i];
        if (macro.isVariadic && token.kind == TokenKind::Identifier &&
            tokenText(token) == "__VA_OPT__" && i + 1 < macro.body.size() &&
            macro.body[i + 1].kind == TokenKind::LParen) {
            std::size_t close = macro.body.size();
            int depth = 0;
            for (std::size_t j = i + 2; j < macro.body.size(); ++j) {
                if (macro.body[j].kind == TokenKind::LParen) {
                    ++depth;
                } else if (macro.body[j].kind == TokenKind::RParen) {
                    if (depth == 0) {
                        close = j;
                        break;
                    }
                    --depth;
                }
            }
            if (close != macro.body.size()) {
                if (hasVariadicArgs) {
                    body.insert(body.end(),
                                macro.body.begin() + static_cast<std::ptrdiff_t>(i + 2),
                                macro.body.begin() + static_cast<std::ptrdiff_t>(close));
                }
                i = close;
                continue;
            }
        }
        body.push_back(token);
    }

    std::vector<Token> substituted;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const Token& token = body[i];
        if (token.kind == TokenKind::Hash && i + 1 < body.size() &&
            body[i + 1].kind == TokenKind::Identifier) {
            if (const std::vector<Token>* arg = getArg(tokenText(body[i + 1]))) {
                std::string rawText;
                bool first = true;
                for (const Token& argToken : *arg) {
                    if (argToken.kind == TokenKind::EndOfDirective) continue;
                    if (!first) rawText += ' ';
                    rawText += tokenText(argToken);
                    first = false;
                }
                const std::string text = "\"" + escapeForStringLiteral(rawText) + "\"";

                ExpansionPiece piece;
                piece.text = text;
                piece.spellingLoc = body[i].loc;
                const FileID expansionFile = sourceManager_.createExpansion(
                    body[i].loc, {std::move(piece)});

                Token stringToken;
                stringToken.kind = TokenKind::StringLiteral;
                stringToken.loc = sourceManager_.makeLocation(expansionFile, 0);
                stringToken.length = static_cast<std::uint32_t>(text.size());
                stringToken.flags = TokenFlags::None;
                substituted.push_back(stringToken);
                ++i;
                continue;
            }
        }

        if (token.kind == TokenKind::Identifier) {
            if (const std::vector<Token>* arg = getArg(tokenText(token))) {
                const bool adjacentPaste =
                    (!substituted.empty() && substituted.back().kind == TokenKind::HashHash) ||
                    (i + 1 < body.size() && body[i + 1].kind == TokenKind::HashHash);
                if (adjacentPaste) {
                    substituted.insert(substituted.end(), arg->begin(), arg->end());
                } else {
                    std::vector<Token> expanded = expandTokenSequence(*arg, hidden);
                    substituted.insert(substituted.end(), expanded.begin(), expanded.end());
                }
                continue;
            }
        }

        substituted.push_back(token);
    }
    return substituted;
}

std::vector<Token> Preprocessor::applyTokenPaste(const std::vector<Token>& tokens,
                                                 SourceLocation loc) {
    std::vector<Token> output;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind != TokenKind::HashHash) {
            output.push_back(tokens[i]);
            continue;
        }
        if (output.empty() || i + 1 >= tokens.size()) {
            error(loc, "invalid ## in macro expansion");
            continue;
        }
        const Token lhs = output.back();
        const Token rhs = tokens[++i];
        const std::string text = tokenText(lhs) + tokenText(rhs);
        Token pasted = synthesizeToken(text, lhs.loc);
        if (pasted.kind == TokenKind::Invalid) {
            error(loc, "token pasting produces invalid token: " + text);
        }
        output.back() = pasted;
    }
    return output;
}


} // namespace rhc
