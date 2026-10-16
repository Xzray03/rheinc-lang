#pragma once

#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhLexer/Lexer.h"
#include "RhPreprocessor/ConditionalStack.h"
#include "RhPreprocessor/HeaderSearch.h"
#include "RhPreprocessor/IncludeState.h"
#include "RhPreprocessor/MacroTable.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace rhc {

class Preprocessor {
public:
    Preprocessor(SourceManager& sourceManager,
                 DiagnosticEngine& diagnostics,
                 IdentifierTable& identifierTable,
                 const LangOptions& options);

    void setMainFile(FileID file);
    void addIncludeDirectory(std::string path);

    bool lex(Token& result);
    bool isDefined(std::string_view name) const;

private:
    bool getRawToken(Token& result);
    bool peekRawToken(Token& result);
    bool lexRawFromLexerStack(Token& result);

    std::vector<Token> readDirectiveTokens();
    void handleDirective(const Token& hashToken);

    void handleTambahkan(const std::vector<Token>& tokens, SourceLocation loc);
    void handleDefinisikan(const std::vector<Token>& tokens, SourceLocation loc);
    void handleHapusDefinisi(const std::vector<Token>& tokens, SourceLocation loc);
    void handleJika(const std::vector<Token>& tokens, SourceLocation loc);
    void handleAkhir(SourceLocation loc);
    void handleErrorWarning(const std::vector<Token>& tokens, SourceLocation loc, bool isError);

    bool expandIfNeeded(const Token& token, std::vector<Token>& output);
    std::vector<Token> expandTokenSequence(std::vector<Token> tokens,
                                           std::unordered_set<std::string> hidden);

    bool invokeObjectLikeMacro(const MacroDefinition& macro,
                               SourceLocation macroNameLoc,
                               std::unordered_set<std::string> hidden,
                               std::vector<Token>& output);
    bool invokeFunctionLikeMacro(const MacroDefinition& macro,
                                 SourceLocation macroNameLoc,
                                 std::unordered_set<std::string> hidden,
                                 std::vector<Token>& output);

    bool invokeFunctionLikeMacroWithArgs(const MacroDefinition& macro,
                                         SourceLocation macroNameLoc,
                                         std::unordered_set<std::string> hidden,
                                         std::vector<std::vector<Token>> args,
                                         std::vector<Token>& output);

    std::vector<std::vector<Token>> parseMacroArguments();
    std::vector<Token> substituteMacroBody(const MacroDefinition& macro,
                                           const std::vector<std::vector<Token>>& args,
                                           const std::unordered_set<std::string>& hidden);
    std::vector<Token> applyTokenPaste(const std::vector<Token>& tokens,
                                       SourceLocation loc);

    std::vector<Token> expandForConditional(std::vector<Token> tokens);
    bool evaluateConditionalExpression(const std::vector<Token>& tokens, long long& result);

    std::string tokenText(const Token& token) const;
    std::vector<ExpansionPiece> makeExpansionPieces(const std::vector<Token>& tokens) const;
    std::vector<Token> remapToExpansion(const std::vector<Token>& tokens,
                                        SourceLocation expansionLoc);
    Token synthesizeToken(std::string text, SourceLocation spellingLoc,
                          TokenKind preferredKind = TokenKind::Unknown);

    FileID currentFile() const;
    void error(SourceLocation loc, const std::string& message);
    void warning(SourceLocation loc, const std::string& message);

    SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
    IdentifierTable& identifierTable_;
    LangOptions options_;
    HeaderSearch headerSearch_;
    MacroTable macros_;
    ConditionalStack conditionals_;
    IncludeState includes_;

    std::vector<std::unique_ptr<Lexer>> lexerStack_;
    std::vector<FileID> lexerFiles_;
    Token peekedRaw_;
    bool hasPeekedRaw_ = false;
    std::vector<Token> expandedBuffer_;
    std::size_t expandedPos_ = 0;
};

} // namespace rhc
