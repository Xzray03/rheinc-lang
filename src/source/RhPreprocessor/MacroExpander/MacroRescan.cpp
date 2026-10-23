#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <cstddef>
#include <utility>

namespace rhc {

using namespace ppdetail;


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

} // namespace rhc
