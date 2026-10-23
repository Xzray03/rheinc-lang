#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <cstddef>
#include <utility>

namespace rhc {

using namespace ppdetail;


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
