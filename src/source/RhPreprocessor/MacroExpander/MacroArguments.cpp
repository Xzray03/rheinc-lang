#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <cstddef>
#include <utility>

namespace rhc {

using namespace ppdetail;


std::vector<std::vector<Token>> Preprocessor::parseMacroArguments() {
    Token open;
    getRawToken(open); // '('
    if (open.kind != TokenKind::LParen) return {};

    bool terminated = false;
    return collectMacroArguments([this](Token& token) { return getRawToken(token); },
                                 terminated);
}

} // namespace rhc
