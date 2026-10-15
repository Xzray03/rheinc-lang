#include "RhParser/Parser.h"

namespace rhc {

DeclNode* Parser::parseTopLevelDeclaration() {
    if (tokens_.is(TokenKind::Minus)) {
        const Token minus = tokens_.consume();
        TestAnnotationDecl* annotation = parseTestAnnotationDeclaration();
        annotation->range = makeRange(minus, tokens_.current());
        return annotation;
    }

    std::vector<FunctionModifier> functionModifiers;
    while (tokens_.is(TokenKind::KwAsinkron) ||
           tokens_.isContextual(ContextualKeywordID::Coroutine) ||
           tokens_.isContextual(ContextualKeywordID::Pembangkit)) {
        const Token modifier = tokens_.consume();
        if (modifier.kind == TokenKind::KwAsinkron) {
            functionModifiers.push_back(FunctionModifier::Async);
        } else if (tokenText(modifier) == "coroutine") {
            functionModifiers.push_back(FunctionModifier::Coroutine);
        } else {
            functionModifiers.push_back(FunctionModifier::Generator);
        }
    }

    if (tokens_.is(TokenKind::KwFungsi)) {
        const Token keyword = tokens_.consume();
        FunctionDecl* decl = parseFunctionLikeDeclaration(keyword, false);
        decl->modifiers = std::move(functionModifiers);
        return decl;
    }
    if (tokens_.is(TokenKind::KwProsedur)) {
        const Token keyword = tokens_.consume();
        FunctionDecl* decl = parseFunctionLikeDeclaration(keyword, true);
        decl->modifiers = std::move(functionModifiers);
        return decl;
    }
    if (!functionModifiers.empty()) {
        errorExpected("function or procedure after modifier");
        return astContext_.create<ErrorDecl>(makeRange(tokens_.current()));
    }
    if (tokens_.is(TokenKind::KwStruktur)) {
        const Token keyword = tokens_.consume();
        return parseStructDeclaration(keyword);
    }
    if (tokens_.is(TokenKind::KwVariabel) || tokens_.is(TokenKind::KwTetap)) {
        return parseGlobalVariableDeclaration();
    }
    if (tokens_.is(TokenKind::KwTugasGlobal)) {
        const Token modifier = tokens_.consume();
        if (!tokens_.is(TokenKind::KwVariabel) && !tokens_.is(TokenKind::KwTetap)) {
            errorExpected("variable after tugas_global");
            return astContext_.create<ErrorDecl>(makeRange(modifier));
        }
        return parseGlobalVariableDeclaration(StorageModifier::TaskGlobal);
    }
    if (tokens_.is(TokenKind::KwGenerik)) {
        const Token keyword = tokens_.consume();
        return parseGenericDeclaration(keyword);
    }
    if (tokens_.is(TokenKind::KwCacahan) ||
        (tokens_.is(TokenKind::Identifier) && tokenText(tokens_.current()) == "cacahan_sum")) {
        const Token keyword = tokens_.consume();
        return parseEnumDeclaration(keyword);
    }
    if (tokens_.is(TokenKind::KwDeklarasi) || tokens_.is(TokenKind::KwPrototipe)) {
        const Token directive = tokens_.consume();
        if (!tokens_.is(TokenKind::KwFungsi) && !tokens_.is(TokenKind::KwProsedur)) {
            errorExpected("function or procedure after declaration directive");
            return astContext_.create<ErrorDecl>(makeRange(directive));
        }
        const Token keyword = tokens_.consume();
        return parseFunctionLikeDeclaration(keyword, keyword.kind == TokenKind::KwProsedur, false);
    }

    const Token bad = tokens_.consume();
    errorExpected("top-level declaration");
    return astContext_.create<ErrorDecl>(makeRange(bad));
}


GlobalVarDecl* Parser::parseGlobalVariableDeclaration(StorageModifier storage) {
    const Token keyword = tokens_.consume();
    const bool isConst = keyword.kind == TokenKind::KwTetap;
    if (!isNameToken(tokens_.current())) {
        errorExpected("global variable name");
        return astContext_.create<GlobalVarDecl>("", nullptr, nullptr, isConst, storage,
                                                 makeRange(keyword));
    }
    const Token name = tokens_.consume();
    TypeReprNode* type = nullptr;
    ExprNode* initializer = nullptr;
    if (tokens_.tryConsume(TokenKind::Colon)) {
        type = parseTypeRepr();
    }
    if (tokens_.tryConsume(TokenKind::Equal)) {
        initializer = parseExpression();
    }
    tokens_.tryConsume(TokenKind::Semi);
    return astContext_.create<GlobalVarDecl>(tokenText(name), type, initializer, isConst,
                                             storage, makeRange(keyword, tokens_.current()));
}


TestAnnotationDecl* Parser::parseTestAnnotationDeclaration() {
    if (!tokens_.is(TokenKind::Identifier) || tokenText(tokens_.current()) != "Tes") {
        errorExpected("'Tes' after '-' test annotation");
        return astContext_.create<TestAnnotationDecl>("", std::vector<ExprNode*>{},
                                                      makeRange(tokens_.current()));
    }
    tokens_.consume(); // Tes

    if (!tokens_.is(TokenKind::Identifier)) {
        errorExpected("test annotation name");
        return astContext_.create<TestAnnotationDecl>("", std::vector<ExprNode*>{},
                                                      makeRange(tokens_.current()));
    }
    const Token name = tokens_.consume();

    std::vector<ExprNode*> arguments;
    if (tokens_.tryConsume(TokenKind::LParen)) {
        while (!tokens_.is(TokenKind::RParen) && tokens_.current().kind != TokenKind::Eof) {
            arguments.push_back(parseExpression());
            if (!tokens_.tryConsume(TokenKind::Comma)) break;
        }
        tokens_.expect(TokenKind::RParen, "')' after test annotation arguments");
    }

    SourceRange range;
    range.begin = name.loc;
    const SourceLocation endLoc = tokens_.current().loc;
    range.length = endLoc >= name.loc
        ? static_cast<std::uint32_t>(endLoc - name.loc)
        : 0;
    return astContext_.create<TestAnnotationDecl>(tokenText(name), std::move(arguments), range);
}

FunctionDecl* Parser::parseFunctionLikeDeclaration(const Token& keywordToken, bool isProcedure,
                                                   bool requireBody, bool allowConstraints) {
    (void)allowConstraints; // constraints are always captured
    if (!tokens_.is(TokenKind::Identifier)) {
        errorExpected("function or procedure name");
        return astContext_.create<FunctionDecl>("", isProcedure, makeRange(keywordToken));
    }

    const Token nameToken = tokens_.consume();
    FunctionDecl* declaration = astContext_.create<FunctionDecl>(
        tokenText(nameToken), isProcedure, makeRange(keywordToken, nameToken));

    if (tokens_.expect(TokenKind::LParen, "'(' after function name")) {
        while (!tokens_.is(TokenKind::RParen) && tokens_.current().kind != TokenKind::Eof) {
            // Generic default syntax in function parameter list:
            // maks(dengan T = bilangan, a: T, b: T)
            if (tokens_.isContextual(ContextualKeywordID::Dengan)) {
                tokens_.consume();
                if (tokens_.is(TokenKind::Identifier) && tokens_.peek().kind == TokenKind::Equal) {
                    tokens_.consume();
                    tokens_.consume();
                    parseTypeRepr();
                }
                tokens_.tryConsume(TokenKind::Comma);
                continue;
            }

            if (!tokens_.is(TokenKind::Identifier)) {
                errorExpected("parameter name");
                break;
            }
            const Token paramName = tokens_.consume();
            TypeReprNode* paramType = astContext_.create<ErrorTypeRepr>(makeRange(paramName));
            SourceRange paramRange = makeRange(paramName);
            if (tokens_.tryConsume(TokenKind::Colon)) {
                paramType = parseTypeRepr();
                paramRange = makeRange(paramName, tokens_.current());
            }
            declaration->parameters.push_back(
                astContext_.create<ParamDecl>(tokenText(paramName), paramType, paramRange));
            if (!tokens_.tryConsume(TokenKind::Comma)) break;
        }
        const Token rparen = tokens_.current();
        tokens_.expect(TokenKind::RParen, "')' after parameter list");
        declaration->range = makeRange(keywordToken, rparen);
    }

    if (!isProcedure && tokens_.tryConsume(TokenKind::Arrow)) {
        declaration->returnType = parseTypeRepr();
    }

    if (tokens_.is(TokenKind::KwDimana)) {
        declaration->constraints = parseDimanaConstraints();
    }

    if (!requireBody || tokens_.is(TokenKind::Semi)) {
        tokens_.tryConsume(TokenKind::Semi);
        declaration->kind = isProcedure ? AstKind::ProcedureDecl : AstKind::FunctionDecl;
        declaration->range = makeRange(keywordToken, tokens_.current());
        return declaration;
    }

    declaration->body = parseBlock();
    declaration->kind = isProcedure ? AstKind::ProcedureDecl : AstKind::FunctionDecl;
    declaration->range = makeRange(keywordToken, tokens_.current());
    return declaration;
}

StructDecl* Parser::parseStructDeclaration(const Token& keywordToken) {
    if (!tokens_.is(TokenKind::Identifier)) {
        errorExpected("struct name");
        return astContext_.create<StructDecl>("", std::vector<StructField*>{},
                                              makeRange(keywordToken));
    }

    const Token name = tokens_.consume();
    StructDecl* declaration = astContext_.create<StructDecl>(
        tokenText(name), std::vector<StructField*>{}, makeRange(keywordToken, name));

    if (!tokens_.expect(TokenKind::LBrace, "'{' after struct name")) {
        return declaration;
    }

    while (!tokens_.is(TokenKind::RBrace) && tokens_.current().kind != TokenKind::Eof) {
        if (!tokens_.is(TokenKind::Identifier)) {
            errorExpected("field name");
            break;
        }
        const Token fieldName = tokens_.consume();
        TypeReprNode* fieldType = nullptr;
        ExprNode* bitWidth = nullptr;
        if (tokens_.tryConsume(TokenKind::Colon)) {
            fieldType = parseTypeRepr();
            if (tokens_.tryConsume(TokenKind::Colon)) {
                bitWidth = parseExpression();
            }
        } else {
            errorExpected("':' after field name");
        }
        declaration->fields.push_back(
            astContext_.create<StructField>(tokenText(fieldName), fieldType, bitWidth,
                                            makeRange(fieldName, tokens_.current())));
        tokens_.tryConsume(TokenKind::Comma);
    }

    const Token rbrace = tokens_.current();
    tokens_.expect(TokenKind::RBrace, "'}' to close struct declaration");
    declaration->range = makeRange(keywordToken, rbrace);
    return declaration;
}

EnumDecl* Parser::parseEnumDeclaration(const Token& keywordToken) {
    if (tokens_.is(TokenKind::KwCacahan) ||
        (tokens_.is(TokenKind::Identifier) &&
         (tokenText(tokens_.current()) == "cacahan_sum" ||
          tokenText(tokens_.current()) == "cacahan"))) {
        tokens_.consume();
    }

    if (!tokens_.is(TokenKind::Identifier)) {
        errorExpected("enum name");
        return astContext_.create<EnumDecl>("", std::vector<EnumField*>{},
                                            makeRange(keywordToken));
    }

    const Token name = tokens_.consume();
    EnumDecl* declaration = astContext_.create<EnumDecl>(
        tokenText(name), std::vector<EnumField*>{}, makeRange(keywordToken, name));

    if (tokens_.is(TokenKind::KwDimana)) {
        declaration->constraints = parseDimanaConstraints();
    }

    if (!tokens_.expect(TokenKind::LBrace, "'{' after enum name")) {
        return declaration;
    }

    while (!tokens_.is(TokenKind::RBrace) && tokens_.current().kind != TokenKind::Eof) {
        if (tokens_.is(TokenKind::KwKasus)) {
            tokens_.consume();
        }
        if (!tokens_.is(TokenKind::Identifier)) {
            errorExpected("enum case name");
            break;
        }
        const Token caseName = tokens_.consume();
        std::vector<std::pair<std::string, TypeReprNode*>> fields;
        if (tokens_.tryConsume(TokenKind::LParen)) {
            while (!tokens_.is(TokenKind::RParen) && tokens_.current().kind != TokenKind::Eof) {
                if (tokens_.is(TokenKind::Identifier) && tokens_.peek().kind == TokenKind::Colon) {
                    const Token fieldName = tokens_.consume();
                    tokens_.consume(); // :
                    TypeReprNode* fieldType = parseTypeRepr();
                    fields.emplace_back(tokenText(fieldName), fieldType);
                } else {
                    TypeReprNode* fieldType = parseTypeRepr();
                    fields.emplace_back(std::string(), fieldType);
                }
                if (!tokens_.tryConsume(TokenKind::Comma)) break;
            }
            tokens_.expect(TokenKind::RParen, "')' after enum case fields");
        }
        ExprNode* aliasValue = nullptr;
        if (tokens_.tryConsume(TokenKind::Equal)) {
            aliasValue = parseExpression();
        }
        declaration->fields.push_back(
            astContext_.create<EnumField>(tokenText(caseName), std::move(fields),
                                          aliasValue,
                                          makeRange(caseName, tokens_.current())));
        tokens_.tryConsume(TokenKind::Comma);
    }

    const Token rbrace = tokens_.current();
    tokens_.expect(TokenKind::RBrace, "'}' to close enum declaration");
    declaration->range = makeRange(keywordToken, rbrace);
    return declaration;
}

TraitConstraintClauses Parser::parseDimanaConstraints() {
    tokens_.consume(); // dimana
    TraitConstraintClauses clauses;
    while (tokens_.is(TokenKind::Identifier)) {
        std::string typeVariable = tokenText(tokens_.consume());
        tokens_.expect(TokenKind::Colon, "':' in trait constraint");
        std::vector<std::string> traits;
        while (tokens_.is(TokenKind::Identifier)) {
            traits.push_back(tokenText(tokens_.consume()));
            if (!tokens_.is(TokenKind::Comma)) break;
            // Comma + Ident + Colon -> separator of the NEXT clause.
            if (tokens_.peek().kind == TokenKind::Identifier &&
                tokens_.peek(2).kind == TokenKind::Colon) {
                break;
            }
            tokens_.consume(); // comma between traits
            if (!tokens_.is(TokenKind::Identifier)) break;
        }
        clauses.emplace_back(std::move(typeVariable), std::move(traits));
        if (!tokens_.tryConsume(TokenKind::Comma)) break;
    }
    return clauses;
}

GenericDecl* Parser::parseGenericDeclaration(const Token& keywordToken) {
    std::vector<std::string> typeParameters;
    while (tokens_.is(TokenKind::Identifier)) {
        typeParameters.push_back(tokenText(tokens_.consume()));
        if (!tokens_.tryConsume(TokenKind::Comma)) break;
    }

    DeclNode* nested = nullptr;
    if (tokens_.is(TokenKind::KwStruktur)) {
        const Token keyword = tokens_.consume();
        nested = parseStructDeclaration(keyword);
    } else if (tokens_.is(TokenKind::KwFungsi) || tokens_.is(TokenKind::KwProsedur)) {
        const Token keyword = tokens_.consume();
        nested = parseFunctionLikeDeclaration(keyword, keyword.kind == TokenKind::KwProsedur,
                                              true, true);
    } else if (tokens_.is(TokenKind::KwCacahan) ||
               (tokens_.is(TokenKind::Identifier) &&
                (tokenText(tokens_.current()) == "cacahan_sum" ||
                 tokenText(tokens_.current()) == "cacahan"))) {
        nested = parseEnumDeclaration(keywordToken);
    } else {
        errorExpected("struct, function, procedure, or cacahan_sum after generik");
        nested = astContext_.create<ErrorDecl>(makeRange(keywordToken));
    }

    SourceRange range;
    range.begin = keywordToken.loc;
    const SourceLocation endLoc = nested->range.begin + nested->range.length;
    range.length = endLoc >= keywordToken.loc
        ? static_cast<std::uint32_t>(endLoc - keywordToken.loc)
        : 0;
    return astContext_.create<GenericDecl>(std::move(typeParameters), nested, range);
}

} // namespace rhc
