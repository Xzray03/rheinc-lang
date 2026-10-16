#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include <utility>

namespace rhc {

using namespace ppdetail;

Preprocessor::Preprocessor(SourceManager& sourceManager,
                           DiagnosticEngine& diagnostics,
                           IdentifierTable& identifierTable,
                           const LangOptions& options)
    : sourceManager_(sourceManager), diagnostics_(diagnostics),
      identifierTable_(identifierTable), options_(options) {}

void Preprocessor::setMainFile(FileID file) {
    lexerStack_.clear();
    lexerFiles_.clear();
    includes_ = IncludeState{};
    macros_ = MacroTable{};
    conditionals_ = ConditionalStack{};
    lexerStack_.push_back(std::make_unique<Lexer>(file, sourceManager_, identifierTable_,
                                                  diagnostics_, options_));
    lexerFiles_.push_back(file);
    includes_.pushActive(sourceManager_.getPath(file), file);
}

void Preprocessor::addIncludeDirectory(std::string path) {
    headerSearch_.addIncludeDirectory(std::move(path));
}

FileID Preprocessor::currentFile() const {
    return lexerFiles_.empty() ? FileID() : lexerFiles_.back();
}

void Preprocessor::error(SourceLocation loc, const std::string& message) {
    diagnostics_.error(sourceManager_.getFileID(loc), loc, 1, message);
}

void Preprocessor::warning(SourceLocation loc, const std::string& message) {
    diagnostics_.warning(sourceManager_.getFileID(loc), loc, 1, message);
}

bool Preprocessor::getRawToken(Token& result) {
    if (hasPeekedRaw_) {
        result = peekedRaw_;
        hasPeekedRaw_ = false;
        return true;
    }
    return lexRawFromLexerStack(result);
}

bool Preprocessor::peekRawToken(Token& result) {
    if (!hasPeekedRaw_) {
        if (!lexRawFromLexerStack(peekedRaw_)) return false;
        hasPeekedRaw_ = true;
    }
    result = peekedRaw_;
    return true;
}

bool Preprocessor::lexRawFromLexerStack(Token& result) {
    while (!lexerStack_.empty()) {
        Token token = lexerStack_.back()->lexRaw();
        if (token.kind == TokenKind::Eof) {
            lexerStack_.pop_back();
            lexerFiles_.pop_back();
            includes_.popActive();
            continue;
        }
        result = token;
        return true;
    }
    result.kind = TokenKind::Eof;
    result.loc = 0;
    result.length = 0;
    result.ident = nullptr;
    result.flags = TokenFlags::None;
    return false;
}

bool Preprocessor::lex(Token& result) {
    while (true) {
        if (expandedPos_ < expandedBuffer_.size()) {
            result = expandedBuffer_[expandedPos_++];
            clearPreprocessorOnlyFlags(result);
            return true;
        }
        expandedBuffer_.clear();
        expandedPos_ = 0;

        Token token;
        if (!getRawToken(token)) return false;

        if (token.kind == TokenKind::Hash) {
            handleDirective(token);
            continue;
        }

        if (!conditionals_.isCurrentActive()) {
            continue;
        }

        std::vector<Token> expanded;
        if (expandIfNeeded(token, expanded)) {
            // Token pertama hasil ekspansi menempati posisi nama makro, jadi ia mewarisi
            // penanda awal baris (mis. `DEKREMEN(x)` di awal baris yang berekspansi ke `--x`).
            if (!expanded.empty() && hasFlag(token.flags, TokenFlags::StartOfLine)) {
                expanded.front().flags = expanded.front().flags | TokenFlags::StartOfLine;
            }
            expandedBuffer_ = std::move(expanded);
            expandedPos_ = 0;
            continue;
        }

        if (token.kind == TokenKind::EndOfDirective) continue;
        result = token;
        clearPreprocessorOnlyFlags(result);
        return true;
    }
}


} // namespace rhc
