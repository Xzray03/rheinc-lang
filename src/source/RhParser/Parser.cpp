#include "RhParser/Parser.h"

namespace rhc {

Parser::Parser(Preprocessor& preprocessor,
               SourceManager& sourceManager,
               DiagnosticEngine& diagnostics)
    : preprocessor_(preprocessor), sourceManager_(sourceManager),
      diagnostics_(diagnostics), tokens_(preprocessor, diagnostics) {}

TranslationUnitDecl* Parser::parseTranslationUnit() {
    TranslationUnitDecl* unit = astContext_.create<TranslationUnitDecl>();
    while (tokens_.current().kind != TokenKind::Eof) {
        DeclNode* declaration = parseTopLevelDeclaration();
        if (auto* annotation = dynamic_cast<TestAnnotationDecl*>(declaration)) {
            unit->testAnnotations.push_back(annotation);
        } else if (declaration) {
            unit->declarations.push_back(declaration);
        }
        else skipToTopLevelSynchronization();
    }
    return unit;
}

SourceRange Parser::makeRange(const Token& token) const {
    SourceRange range;
    range.begin = token.loc;
    range.length = token.length;
    return range;
}

SourceRange Parser::makeRange(const Token& begin, const Token& end) const {
    SourceRange range;
    range.begin = begin.loc;
    const SourceLocation endLoc = end.loc + end.length;
    range.length = endLoc >= begin.loc ? static_cast<std::uint32_t>(endLoc - begin.loc) : 0;
    return range;
}

bool Parser::isNameToken(const Token& token) const {
    return token.kind == TokenKind::Identifier;
}

std::string Parser::tokenText(const Token& token) const {
    return std::string(sourceManager_.getText(sourceManager_.getFileID(token.loc),
                                              token.loc,
                                              token.length));
}

void Parser::errorExpected(const char* what) {
    const Token& token = tokens_.current();
    diagnostics_.error(sourceManager_.getFileID(token.loc), token.loc, token.length,
                       std::string("expected ") + what);
}

void Parser::skipToTopLevelSynchronization() {
    while (tokens_.current().kind != TokenKind::Eof &&
           tokens_.current().kind != TokenKind::KwFungsi &&
           tokens_.current().kind != TokenKind::KwProsedur) {
        tokens_.consume();
    }
}

} // namespace rhc
