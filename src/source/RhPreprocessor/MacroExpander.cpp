#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <utility>

namespace rhc {

using namespace ppdetail;

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
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::Identifier && token.ident) {
            const std::string name(token.ident->getName());
            if (hidden.contains(name)) {
                output.push_back(token);
                continue;
            }
            const MacroDefinition* macro = macros_.find(name);
            if (!macro || macro->isFunctionLike) {
                output.push_back(token);
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
    hidden.insert(macro.name);
    const std::vector<std::vector<Token>> args = parseMacroArguments();
    std::vector<Token> substituted = substituteMacroBody(macro, args, hidden);
    std::vector<Token> pasted = applyTokenPaste(substituted, macroNameLoc);
    std::vector<Token> mapped = remapToExpansion(pasted, macroNameLoc);
    output = expandTokenSequence(std::move(mapped), std::move(hidden));
    return true;
}

std::vector<std::vector<Token>> Preprocessor::parseMacroArguments() {
    std::vector<std::vector<Token>> args;
    std::vector<Token> current;
    int depth = 0;

    Token open;
    getRawToken(open); // '('
    if (open.kind != TokenKind::LParen) return args;

    while (true) {
        Token token;
        if (!getRawToken(token)) break;
        if (token.kind == TokenKind::EndOfDirective) continue;
        if (token.kind == TokenKind::LParen) {
            ++depth;
            current.push_back(token);
            continue;
        }
        if (token.kind == TokenKind::RParen) {
            if (depth == 0) {
                if (!current.empty() || args.empty()) args.push_back(std::move(current));
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

    std::vector<Token> substituted;
    for (std::size_t i = 0; i < macro.body.size(); ++i) {
        const Token& token = macro.body[i];
        if (token.kind == TokenKind::Hash && i + 1 < macro.body.size() &&
            macro.body[i + 1].kind == TokenKind::Identifier) {
            if (const std::vector<Token>* arg = getArg(tokenText(macro.body[i + 1]))) {
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
                piece.spellingLoc = macro.body[i].loc;
                const FileID expansionFile = sourceManager_.createExpansion(
                    macro.body[i].loc, {std::move(piece)});

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
                    (i + 1 < macro.body.size() && macro.body[i + 1].kind == TokenKind::HashHash);
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
