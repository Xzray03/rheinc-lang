#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <cstddef>
#include <utility>

namespace rhc {

using namespace ppdetail;


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

} // namespace rhc
