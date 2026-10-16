#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"

namespace rhc {

using namespace ppdetail;

std::vector<Token> Preprocessor::readDirectiveTokens() {
    std::vector<Token> tokens;
    while (true) {
        Token token;
        if (!getRawToken(token)) break;
        if (token.kind == TokenKind::EndOfDirective) break;
        if (token.kind == TokenKind::Backslash) {
            Token next;
            if (peekRawToken(next) && next.kind == TokenKind::EndOfDirective) {
                getRawToken(next);
                continue;
            }
        }
        tokens.push_back(token);
    }
    return tokens;
}

void Preprocessor::handleDirective(const Token& hashToken) {
    std::vector<Token> tokens = readDirectiveTokens();
    if (tokens.empty()) return;

    if (isIdentifierNamed(tokens[0], "tambahkan", sourceManager_)) {
        handleTambahkan(tokens, hashToken.loc);
        return;
    }
    if (isIdentifierNamed(tokens[0], "definisikan", sourceManager_)) {
        handleDefinisikan(tokens, hashToken.loc);
        return;
    }
    if (isIdentifierNamed(tokens[0], "hapus", sourceManager_)) {
        handleHapusDefinisi(tokens, hashToken.loc);
        return;
    }
    if (isIdentifierNamed(tokens[0], "jika", sourceManager_)) {
        handleJika(tokens, hashToken.loc);
        return;
    }
    if (isIdentifierNamed(tokens[0], "akhir", sourceManager_)) {
        handleAkhir(hashToken.loc);
        return;
    }
    if (isIdentifierNamed(tokens[0], "error", sourceManager_)) {
        handleErrorWarning(tokens, hashToken.loc, true);
        return;
    }
    if (isIdentifierNamed(tokens[0], "warning", sourceManager_)) {
        handleErrorWarning(tokens, hashToken.loc, false);
        return;
    }

    error(hashToken.loc, "unknown preprocessor directive");
}

void Preprocessor::handleTambahkan(const std::vector<Token>& tokens, SourceLocation loc) {
    if (!conditionals_.isCurrentActive()) return;
    if (tokens.size() < 2 || tokens[1].kind != TokenKind::StringLiteral) {
        error(loc, "#tambahkan expects a quoted header path");
        return;
    }

    std::string includePath(tokenText(tokens[1]));
    if (includePath.size() >= 2 && includePath.front() == '"' && includePath.back() == '"') {
        includePath = includePath.substr(1, includePath.size() - 2);
    }

    const auto resolved = headerSearch_.resolveQuoted(sourceManager_, currentFile(), includePath);
    if (!resolved) {
        error(tokens[1].loc, "include file not found: " + includePath);
        return;
    }

    const std::string& canonical = *resolved;
    if (includes_.isActive(canonical)) {
        error(tokens[1].loc, "circular include detected: " + includePath);
        return;
    }

    if (includes_.alreadyIncluded(canonical)) {
        // Include-once semantics: repeated inclusion emits no tokens and points to
        // the same FileID through IncludedFileSet.
        return;
    }

    std::string loadError;
    const FileID file = sourceManager_.addFileFromDisk(canonical, loadError);
    if (!file.valid()) {
        error(tokens[1].loc, loadError);
        return;
    }

    includes_.markIncluded(canonical, file);
    includes_.pushActive(canonical, file);
    lexerFiles_.push_back(file);
    lexerStack_.push_back(std::make_unique<Lexer>(file, sourceManager_, identifierTable_,
                                                  diagnostics_, options_));
}

void Preprocessor::handleDefinisikan(const std::vector<Token>& tokens, SourceLocation loc) {
    if (!conditionals_.isCurrentActive()) return;
    if (tokens.size() < 2 || tokens[1].kind != TokenKind::Identifier) {
        error(loc, "#definisikan expects a macro name");
        return;
    }

    MacroDefinition macro;
    macro.name = std::string(tokenText(tokens[1]));

    std::size_t pos = 2;
    if (pos < tokens.size() && tokens[pos].kind == TokenKind::LParen &&
        !hasFlag(tokens[pos].flags, TokenFlags::LeadingWhitespace)) {
        macro.isFunctionLike = true;
        ++pos;
        while (pos < tokens.size() && tokens[pos].kind != TokenKind::RParen) {
            if (isPeriodSequence(tokens, pos, 3, sourceManager_)) {
                macro.isVariadic = true;
                macro.parameters.emplace_back("__VA_ARGS__");
                pos += 3;
                if (pos < tokens.size() && tokens[pos].kind != TokenKind::RParen) {
                    error(tokens[pos].loc, "'...' must be the last macro parameter");
                    return;
                }
                continue;
            }
            if (tokens[pos].kind != TokenKind::Identifier) {
                error(tokens[pos].loc, "invalid macro parameter");
                return;
            }
            macro.parameters.emplace_back(tokenText(tokens[pos]));
            ++pos;
            if (pos < tokens.size() && tokens[pos].kind == TokenKind::Comma) ++pos;
        }
        if (pos >= tokens.size()) {
            error(loc, "unterminated macro parameter list");
            return;
        }
        ++pos; // r_paren
    }

    for (; pos < tokens.size(); ++pos) {
        macro.body.push_back(tokens[pos]);
    }

    // __VA_ARGS__ and __VA_OPT__ only make sense inside a variadic macro body, and
    // __VA_OPT__ needs a balanced, non-nested "( ... )" group.
    for (std::size_t i = 0; i < macro.body.size(); ++i) {
        const Token& token = macro.body[i];
        if (token.kind != TokenKind::Identifier) continue;
        const std::string text = tokenText(token);
        const bool isVaArgs = text == "__VA_ARGS__";
        const bool isVaOpt = text == "__VA_OPT__";
        if (!isVaArgs && !isVaOpt) continue;
        if (!macro.isVariadic) {
            error(token.loc, text +
                                 " can only appear in the expansion of a variadic macro");
            return;
        }
        if (!isVaOpt) continue;
        if (i + 1 >= macro.body.size() || macro.body[i + 1].kind != TokenKind::LParen) {
            error(token.loc, "__VA_OPT__ must be followed by '('");
            return;
        }
        int depth = 0;
        std::size_t close = macro.body.size();
        for (std::size_t j = i + 2; j < macro.body.size(); ++j) {
            const Token& inner = macro.body[j];
            if (inner.kind == TokenKind::Identifier && tokenText(inner) == "__VA_OPT__") {
                error(inner.loc, "__VA_OPT__ may not appear inside __VA_OPT__");
                return;
            }
            if (inner.kind == TokenKind::LParen) {
                ++depth;
            } else if (inner.kind == TokenKind::RParen) {
                if (depth == 0) {
                    close = j;
                    break;
                }
                --depth;
            }
        }
        if (close == macro.body.size()) {
            error(token.loc, "unterminated __VA_OPT__");
            return;
        }
        i = close;
    }

    if (!macros_.define(macro)) {
        error(tokens[1].loc, "macro redefinition is not allowed: " + macro.name);
    }
}

void Preprocessor::handleHapusDefinisi(const std::vector<Token>& tokens, SourceLocation loc) {
    if (!conditionals_.isCurrentActive()) return;
    if (tokens.size() < 3 || !isIdentifierNamed(tokens[1], "definisi", sourceManager_) ||
        tokens[2].kind != TokenKind::Identifier) {
        error(loc, "#hapus definisi expects a macro name");
        return;
    }
    macros_.undefine(tokenText(tokens[2]));
}

void Preprocessor::handleJika(const std::vector<Token>& tokens, SourceLocation loc) {
    std::vector<Token> args(tokens.begin() + 1, tokens.end());
    const bool parentActive = conditionals_.currentParentActive();

    if (args.size() == 3 && isIdentifierNamed(args[0], "bukan", sourceManager_) &&
        isIdentifierNamed(args[1], "semua", sourceManager_) &&
        isIdentifierNamed(args[2], "diatas", sourceManager_)) {
        if (conditionals_.empty()) {
            error(loc, "#jika bukan semua diatas without matching #jika");
            return;
        }
        if (!conditionals_.beginElse(loc)) {
            error(loc, "duplicate #jika bukan semua diatas");
        }
        return;
    }

    if (conditionals_.empty()) {
        long long value = 0;
        std::vector<Token> expression = expandForConditional(std::move(args));
        if (!evaluateConditionalExpression(expression, value)) {
            error(loc, "invalid #jika expression");
            conditionals_.beginIf(false, parentActive, loc);
            return;
        }
        conditionals_.beginIf(value != 0, parentActive, loc);
        return;
    }

    if (conditionals_.currentSeenElse()) {
        error(loc, "#jika cannot appear after #jika bukan semua diatas");
        return;
    }

    long long value = 0;
    std::vector<Token> expression = expandForConditional(std::move(args));
    if (!evaluateConditionalExpression(expression, value)) {
        error(loc, "invalid #jika expression");
        conditionals_.beginElseIf(false, loc);
        return;
    }
    conditionals_.beginElseIf(value != 0, loc);
}

void Preprocessor::handleAkhir(SourceLocation loc) {
    if (!conditionals_.end(loc)) {
        error(loc, "#akhir without matching #jika");
    }
}

void Preprocessor::handleErrorWarning(const std::vector<Token>& tokens, SourceLocation loc,
                                      bool isError) {
    if (!conditionals_.isCurrentActive()) return;
    std::string message;
    if (tokens.size() >= 2 && tokens[1].kind == TokenKind::StringLiteral) {
        message = tokenText(tokens[1]);
    } else {
        message = isError ? "#error" : "#warning";
    }
    if (isError) error(loc, message);
    else warning(loc, message);
}


} // namespace rhc
