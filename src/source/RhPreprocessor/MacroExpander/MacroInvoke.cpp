#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <cstddef>
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

} // namespace rhc
