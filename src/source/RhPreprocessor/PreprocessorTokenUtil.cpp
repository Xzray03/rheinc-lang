#include "RhPreprocessor/Preprocessor.h"
#include "RhPreprocessor/PreprocessorInternal.h"
#include "RhLexer/KeywordTable.h"
#include "RhLexer/OperatorTable.h"
#include <utility>

namespace rhc {

using namespace ppdetail;

std::string Preprocessor::tokenText(const Token& token) const {
    return std::string(sourceManager_.getText(sourceManager_.getFileID(token.loc),
                                              token.loc,
                                              token.length));
}

std::vector<ExpansionPiece> Preprocessor::makeExpansionPieces(const std::vector<Token>& tokens) const {
    std::vector<ExpansionPiece> pieces;
    pieces.reserve(tokens.size());
    for (const Token& token : tokens) {
        ExpansionPiece piece;
        piece.text = tokenText(token);
        piece.spellingLoc = token.loc;
        pieces.push_back(std::move(piece));
    }
    return pieces;
}

std::vector<Token> Preprocessor::remapToExpansion(const std::vector<Token>& tokens,
                                                  SourceLocation expansionLoc) {
    const FileID expansionFile = sourceManager_.createExpansion(
        expansionLoc, makeExpansionPieces(tokens));
    std::vector<Token> result = tokens;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].loc = sourceManager_.makeLocation(expansionFile, static_cast<FileOffset>(i));
        result[i].length = static_cast<std::uint32_t>(tokenText(result[i]).size());
    }
    return result;
}

Token Preprocessor::synthesizeToken(std::string text, SourceLocation spellingLoc,
                                    TokenKind preferredKind) {
    ExpansionPiece piece;
    piece.text = text;
    piece.spellingLoc = spellingLoc;
    const FileID expansionFile = sourceManager_.createExpansion(
        spellingLoc, {std::move(piece)});

    Token token;
    token.loc = sourceManager_.makeLocation(expansionFile, 0);
    token.length = static_cast<std::uint32_t>(text.size());
    token.flags = TokenFlags::None;

    if (preferredKind == TokenKind::StringLiteral) {
        token.kind = TokenKind::StringLiteral;
        return token;
    }

    if (!text.empty() && (std::isalpha(static_cast<unsigned char>(text[0])) || text[0] == '_' ||
        static_cast<unsigned char>(text[0]) >= 0x80)) {
        IdentifierInfo& info = identifierTable_.intern(text);
        token.kind = info.isReservedKeyword() ? info.getReservedKeywordKind()
                                              : TokenKind::Identifier;
        token.ident = &info;
        return token;
    }

    if (text.size() <= 3) {
        const TokenKind op = OperatorTable::lookup(text);
        if (op != TokenKind::Unknown) {
            token.kind = op;
            return token;
        }
    }

    bool maybeNumber = !text.empty();
    bool maybeFloat = false;
    for (char c : text) {
        if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == 'E' ||
              c == '+' || c == '-' || c == 'x' || c == 'X' ||
              (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X') &&
               std::isxdigit(static_cast<unsigned char>(c))))) {
            maybeNumber = false;
            break;
        }
        if (c == '.' || c == 'e' || c == 'E') maybeFloat = true;
    }
    if (maybeNumber) {
        token.kind = maybeFloat ? TokenKind::NumericConstant : TokenKind::NumericConstant;
        return token;
    }

    token.kind = TokenKind::Invalid;
    return token;
}


} // namespace rhc
