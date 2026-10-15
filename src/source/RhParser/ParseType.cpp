#include "RhParser/Parser.h"

namespace rhc {
namespace {

bool isBuiltinScalarToken(TokenKind kind) {
    switch (kind) {
    case TokenKind::KwBilangan:
    case TokenKind::KwBilangan8:
    case TokenKind::KwBilangan16:
    case TokenKind::KwBilangan32:
    case TokenKind::KwBilangan64:
    case TokenKind::KwBilanganTanpaTanda8:
    case TokenKind::KwBilanganTanpaTanda16:
    case TokenKind::KwBilanganTanpaTanda32:
    case TokenKind::KwBilanganTanpaTanda64:
    case TokenKind::KwDesimal:
    case TokenKind::KwLogika:
    case TokenKind::KwKarakter:
    case TokenKind::KwTeks:
    case TokenKind::KwHampa:
        return true;
    default:
        return false;
    }
}

bool isTypeArgumentStart(const Token& token) {
    return isBuiltinScalarToken(token.kind) ||
           token.kind == TokenKind::KwBilanganKompleks ||
           token.kind == TokenKind::KwDesimalTetap ||
           token.kind == TokenKind::KwBilanganTanpaBatas ||
           token.kind == TokenKind::KwTunjuk ||
           token.kind == TokenKind::KwLarik ||
           token.kind == TokenKind::KwMatriks ||
           token.kind == TokenKind::Identifier;
}

} // namespace

std::vector<TypeArgumentRepr*> Parser::parseTypeArgumentList(bool angleBracket) {
    std::vector<TypeArgumentRepr*> arguments;
    const Token open = tokens_.consume();
    (void)open;

    while (!tokens_.is(angleBracket ? TokenKind::Greater : TokenKind::RParen) &&
           tokens_.current().kind != TokenKind::Eof) {
        if (!angleBracket && tokens_.isContextual(ContextualKeywordID::Dengan)) {
            tokens_.consume();
        }

        const Token start = tokens_.current();
        std::string name;
        const bool named = tokens_.is(TokenKind::Identifier) &&
                           tokens_.peek().kind == TokenKind::Equal;
        if (named) {
            name = tokenText(tokens_.consume());
            tokens_.consume(); // =
        }

        TypeReprNode* typeArgument = nullptr;
        ExprNode* valueArgument = nullptr;
        if (isTypeArgumentStart(tokens_.current())) {
            typeArgument = parseTypeRepr();
        } else {
            valueArgument = parseExpression();
        }

        SourceRange range;
        range.begin = start.loc;
        const SourceLocation endLoc = tokens_.current().loc;
        range.length = endLoc >= start.loc
            ? static_cast<std::uint32_t>(endLoc - start.loc)
            : 0;
        arguments.push_back(astContext_.create<TypeArgumentRepr>(
            std::move(name), typeArgument, valueArgument, range));

        if (!tokens_.tryConsume(TokenKind::Comma)) break;
    }

    tokens_.expect(angleBracket ? TokenKind::Greater : TokenKind::RParen,
                   angleBracket ? "'>' after generic type arguments"
                                : "')' after type arguments");
    return arguments;
}

TypeReprNode* Parser::parseTypeRepr() {
    TypeReprNode* base = parseTypeReprPrimary();
    // Sized generic alias: `larik_kd[3]` == `larik[3] dari larik_kd`.
    std::vector<ExprNode*> dimensions;
    const Token firstBracket = tokens_.current();
    while (tokens_.tryConsume(TokenKind::LBracket)) {
        dimensions.push_back(parseExpression());
        tokens_.expect(TokenKind::RBracket, "']' in type");
    }
    if (!dimensions.empty()) {
        return astContext_.create<ArrayTypeRepr>(std::move(dimensions), base,
                                                 makeRange(firstBracket, tokens_.current()));
    }
    return base;
}

TypeReprNode* Parser::parseTypeReprPrimary() {
    const Token token = tokens_.current();

    if ((token.kind == TokenKind::Identifier ||
         token.kind == TokenKind::KwMatriks ||
         token.kind == TokenKind::KwDesimalTetap ||
         token.kind == TokenKind::KwBilanganKompleks ||
         token.kind == TokenKind::KwBilanganTanpaBatas) &&
        (tokens_.peek().kind == TokenKind::LParen || tokens_.peek().kind == TokenKind::Less)) {
        const bool angleBracket = tokens_.peek().kind == TokenKind::Less;
        tokens_.consume(); // name
        auto arguments = parseTypeArgumentList(angleBracket);
        return astContext_.create<CallTypeRepr>(tokenText(token), std::move(arguments),
                                                angleBracket,
                                                makeRange(token, tokens_.current()));
    }

    switch (token.kind) {
    case TokenKind::KwBilangan:
    case TokenKind::KwBilangan8:
    case TokenKind::KwBilangan16:
    case TokenKind::KwBilangan32:
    case TokenKind::KwBilangan64:
    case TokenKind::KwBilanganTanpaTanda8:
    case TokenKind::KwBilanganTanpaTanda16:
    case TokenKind::KwBilanganTanpaTanda32:
    case TokenKind::KwBilanganTanpaTanda64:
    case TokenKind::KwDesimal:
    case TokenKind::KwDesimalTetap:
    case TokenKind::KwBilanganTanpaBatas:
    case TokenKind::KwBilanganKompleks:
    case TokenKind::KwLogika:
    case TokenKind::KwKarakter:
    case TokenKind::KwTeks:
    case TokenKind::KwHampa:
    case TokenKind::KwMatriks:
        tokens_.consume();
        return astContext_.create<BuiltinTypeRepr>(token.kind, makeRange(token));

    case TokenKind::KwLarik: {
        tokens_.consume();
        std::vector<ExprNode*> dimensions;
        while (tokens_.tryConsume(TokenKind::LBracket)) {
            dimensions.push_back(parseExpression());
            tokens_.expect(TokenKind::RBracket, "']' in array type");
        }
        tokens_.tryConsume(TokenKind::KwDari);
        TypeReprNode* elementType = parseTypeRepr();
        return astContext_.create<ArrayTypeRepr>(std::move(dimensions), elementType,
                                                 makeRange(token, tokens_.current()));
    }

    case TokenKind::KwTunjuk: {
        tokens_.consume();
        tokens_.tryConsumeContextual(ContextualKeywordID::Ke);
        if (tokens_.is(TokenKind::KwFungsi)) {
            tokens_.consume();
            std::vector<std::pair<std::string, TypeReprNode*>> params;
            TypeReprNode* returnType = nullptr;
            bool bare = true;
            if (tokens_.tryConsume(TokenKind::LParen)) {
                bare = false;
                while (!tokens_.is(TokenKind::RParen) && tokens_.current().kind != TokenKind::Eof) {
                    if (!tokens_.is(TokenKind::Identifier)) {
                        errorExpected("function pointer parameter name");
                        break;
                    }
                    const Token paramName = tokens_.consume();
                    TypeReprNode* paramType = nullptr;
                    if (tokens_.tryConsume(TokenKind::Colon)) {
                        paramType = parseTypeRepr();
                    }
                    params.emplace_back(tokenText(paramName), paramType);
                    if (!tokens_.tryConsume(TokenKind::Comma)) break;
                }
                tokens_.expect(TokenKind::RParen, "')' in function pointer type");
                if (tokens_.tryConsume(TokenKind::Arrow)) {
                    returnType = parseTypeRepr();
                }
            }
            return astContext_.create<FunctionPointerTypeRepr>(
                std::move(params), returnType, bare, makeRange(token, tokens_.current()));
        }
        TypeReprNode* pointee = parseTypeRepr();
        return astContext_.create<PointerTypeRepr>(pointee,
                                                   makeRange(token, tokens_.current()));
    }

    case TokenKind::Identifier: {
        tokens_.consume();
        if (!tokens_.is(TokenKind::Period)) {
            // User-defined generic call syntax: Hasil(dengan T = X), Pasangan(teks).
            if (tokens_.is(TokenKind::LParen) || tokens_.is(TokenKind::Less)) {
                const bool angleBracket = tokens_.is(TokenKind::Less);
                auto arguments = parseTypeArgumentList(angleBracket);
                return astContext_.create<CallTypeRepr>(tokenText(token), std::move(arguments),
                                                        angleBracket,
                                                        makeRange(token, tokens_.current()));
            }
            return astContext_.create<NamedTypeRepr>(tokenText(token), makeRange(token));
        }
        std::string qualifiedName = tokenText(token);
        while (tokens_.tryConsume(TokenKind::Period)) {
            if (!tokens_.is(TokenKind::Identifier)) {
                errorExpected("type member after '.'");
                break;
            }
            qualifiedName += '.';
            qualifiedName += tokenText(tokens_.consume());
        }
        return astContext_.create<NamedTypeRepr>(std::move(qualifiedName),
                                                 makeRange(token, tokens_.current()));
    }

    default:
        errorExpected("type");
        return astContext_.create<ErrorTypeRepr>(makeRange(token));
    }
}

} // namespace rhc
