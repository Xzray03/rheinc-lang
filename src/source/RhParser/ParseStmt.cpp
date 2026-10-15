#include "RhParser/Parser.h"

namespace rhc {

CompoundStmt* Parser::parseBlock() {
    const Token lbrace = tokens_.current();
    if (!tokens_.expect(TokenKind::LBrace, "'{' to start block")) {
        return astContext_.create<CompoundStmt>(makeRange(lbrace));
    }

    CompoundStmt* block = astContext_.create<CompoundStmt>(makeRange(lbrace));
    while (!tokens_.is(TokenKind::RBrace) && tokens_.current().kind != TokenKind::Eof) {
        if (tokens_.tryConsume(TokenKind::Semi)) continue;
        StmtNode* statement = parseStatement();
        if (statement) block->statements.push_back(statement);
    }

    const Token rbrace = tokens_.current();
    tokens_.expect(TokenKind::RBrace, "'}' to close block");
    block->range = makeRange(lbrace, rbrace);
    return block;
}

StmtNode* Parser::parseStatement() {
    if (tokens_.is(TokenKind::KwTugasLokal) || tokens_.is(TokenKind::KwTugasGlobal)) {
        const StorageModifier storage = tokens_.is(TokenKind::KwTugasGlobal)
            ? StorageModifier::TaskGlobal
            : StorageModifier::TaskLocal;
        tokens_.consume();
        return parseVariableStatement(storage);
    }
    switch (tokens_.current().kind) {
    case TokenKind::KwVariabel:
    case TokenKind::KwTetap:
        return parseVariableStatement();
    case TokenKind::KwJika:
        return parseIfStatement();
    case TokenKind::KwStruktur:
        return astContext_.create<DeclStmt>(parseStructDeclaration(tokens_.consume()),
                                            makeRange(tokens_.current()));
    case TokenKind::KwUntuk:
        return parseForStatement();
    case TokenKind::KwSelama:
        return parseWhileStatement();
    case TokenKind::KwUlangi:
        return parseDoWhileStatement();
    case TokenKind::KwBerhenti:
        return parseBreakStatement();
    case TokenKind::KwLanjut:
        return parseContinueStatement();
    case TokenKind::KwPilih:
        return parsePilihStatement();
    case TokenKind::KwJamin:
        return parseJaminStatement();
    case TokenKind::KwJeda: {
        const Token keyword = tokens_.consume();
        ExprNode* condition = nullptr;
        if (tokens_.tryConsume(TokenKind::KwSampai)) {
            condition = parseExpression();
        }
        return astContext_.create<JedaStmt>(condition, makeRange(keyword, tokens_.current()));
    }
    case TokenKind::KwKembalikan:
        return parseReturnStatement();
    case TokenKind::LBrace:
        return parseBlock();
    default:
        if (tokens_.is(TokenKind::Identifier) && tokenText(tokens_.current()) == "keluar") {
            return parseKeluarStatement();
        }
        if (tokens_.is(TokenKind::Identifier) && tokenText(tokens_.current()) == "goto") {
            return parseGotoStatement();
        }
        if (tokens_.is(TokenKind::Identifier) && tokens_.peek().kind == TokenKind::Colon) {
            return parseLabelStatement(tokens_.consume());
        }
        return parseExpressionStatement();
    }
}

StmtNode* Parser::parseVariableStatement(StorageModifier storage) {
    const Token keyword = tokens_.consume();
    const bool isConst = keyword.kind == TokenKind::KwTetap;

    if (!isNameToken(tokens_.current())) {
        errorExpected("variable name");
        return astContext_.create<ErrorStmt>(makeRange(keyword));
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

    return astContext_.create<VarDeclStmt>(tokenText(name), type, initializer, isConst,
                                           makeRange(keyword, tokens_.current()), storage);
}

IfStmt* Parser::parseIfStatement() {
    const Token keyword = tokens_.consume();
    ExprNode* condition = parseExpression();
    tokens_.expect(TokenKind::KwMaka, "'maka' after if condition");
    StmtNode* thenBranch = parseStatement();
    StmtNode* elseBranch = nullptr;

    if (tokens_.isContextual(ContextualKeywordID::Selain)) {
        tokens_.consume();
        tokens_.tryConsumeContextual(ContextualKeywordID::Itu);
        elseBranch = parseStatement();
    } else if (tokens_.is(TokenKind::KwJika) &&
               tokens_.peek().kind == TokenKind::Identifier &&
               tokens_.peek().ident &&
               tokens_.peek().ident->getContextualKeywordID() == ContextualKeywordID::Bukan) {
        tokens_.consume(); // jika
        tokens_.consume(); // bukan
        tokens_.tryConsumeContextual(ContextualKeywordID::Semua);
        tokens_.tryConsumeContextual(ContextualKeywordID::Diatas);
        elseBranch = parseStatement();
    }

    return astContext_.create<IfStmt>(condition, thenBranch, elseBranch,
                                      makeRange(keyword, tokens_.current()));
}

ForStmt* Parser::parseForStatement() {
    const Token keyword = tokens_.consume();

    if (tokens_.isContextual(ContextualKeywordID::Setiap)) {
        tokens_.consume();
        if (!tokens_.is(TokenKind::Identifier)) {
            errorExpected("for-each variable");
            return astContext_.create<ForStmt>("", std::vector<std::string>{}, nullptr, nullptr, nullptr, nullptr,
                                               astContext_.create<ErrorStmt>(makeRange(keyword)),
                                               true, makeRange(keyword));
        }
        std::vector<std::string> variables;
        const Token firstVariable = tokens_.consume();
        variables.push_back(tokenText(firstVariable));
        while (tokens_.tryConsume(TokenKind::Comma)) {
            if (!tokens_.is(TokenKind::Identifier)) {
                errorExpected("for-each variable");
                break;
            }
            variables.push_back(tokenText(tokens_.consume()));
        }
        tokens_.tryConsumeContextual(ContextualKeywordID::Dalam);
        ExprNode* iterable = parseExpression();
        // `untuk setiap x dalam items async` — async for-each modifier.
        if (tokens_.is(TokenKind::Identifier) && tokenText(tokens_.current()) == "async") {
            tokens_.consume();
        }
        ExprNode* stepValue = nullptr;
        if (tokens_.isContextual(ContextualKeywordID::Langkah)) {
            tokens_.consume();
            stepValue = parseExpression();
        }
        StmtNode* body = parseStatement();
        return astContext_.create<ForStmt>(variables.front(), std::move(variables),
                                           nullptr, nullptr, stepValue,
                                           iterable, body, true,
                                           makeRange(keyword, tokens_.current()));
    }

    if (!tokens_.is(TokenKind::Identifier)) {
        errorExpected("loop variable");
        return astContext_.create<ForStmt>("", std::vector<std::string>{}, nullptr, nullptr, nullptr, nullptr,
                                           astContext_.create<ErrorStmt>(makeRange(keyword)),
                                           false, makeRange(keyword));
    }

    const Token variable = tokens_.consume();
    tokens_.expect(TokenKind::KwDari, "'dari' in for statement");
    ExprNode* startValue = parseExpression();
    tokens_.expect(TokenKind::KwSampai, "'sampai' in for statement");
    ExprNode* endValue = parseExpression();
    ExprNode* stepValue = nullptr;
    if (tokens_.isContextual(ContextualKeywordID::Langkah)) {
        tokens_.consume();
        stepValue = parseExpression();
    }
    StmtNode* body = parseStatement();
    return astContext_.create<ForStmt>(tokenText(variable), std::vector<std::string>{tokenText(variable)},
                                       startValue, endValue, stepValue,
                                       nullptr, body, false,
                                       makeRange(keyword, tokens_.current()));
}

WhileStmt* Parser::parseWhileStatement() {
    const Token keyword = tokens_.consume();
    ExprNode* condition = parseExpression();
    StmtNode* body = parseStatement();
    return astContext_.create<WhileStmt>(condition, body,
                                         makeRange(keyword, tokens_.current()));
}

DoWhileStmt* Parser::parseDoWhileStatement() {
    const Token keyword = tokens_.consume();
    StmtNode* body = parseStatement();
    tokens_.expect(TokenKind::KwSelama, "'selama' after ulangi block");
    ExprNode* condition = parseExpression();
    return astContext_.create<DoWhileStmt>(body, condition,
                                           makeRange(keyword, tokens_.current()));
}

BreakStmt* Parser::parseBreakStatement() {
    const Token keyword = tokens_.consume();
    tokens_.tryConsume(TokenKind::Semi);
    return astContext_.create<BreakStmt>(makeRange(keyword));
}

ContinueStmt* Parser::parseContinueStatement() {
    const Token keyword = tokens_.consume();
    tokens_.tryConsume(TokenKind::Semi);
    return astContext_.create<ContinueStmt>(makeRange(keyword));
}

CompoundStmt* Parser::parseCaseBody() {
    if (tokens_.is(TokenKind::LBrace)) {
        return parseBlock();
    }

    const Token start = tokens_.current();
    CompoundStmt* body = astContext_.create<CompoundStmt>(makeRange(start));
    while (!tokens_.is(TokenKind::RBrace) &&
           tokens_.current().kind != TokenKind::Eof &&
           tokens_.current().kind != TokenKind::KwKasus &&
           !tokens_.isContextual(ContextualKeywordID::Bawaan)) {
        StmtNode* statement = parseStatement();
        if (statement) body->statements.push_back(statement);
    }
    body->range = makeRange(start, tokens_.current());
    return body;
}


PatternNode* Parser::parseCasePattern() {
    const Token token = tokens_.current();
    if (token.kind == TokenKind::Identifier && tokens_.peek().kind == TokenKind::LParen) {
        return parseConstructorPattern(tokens_.consume());
    }

    ExprNode* expression = parseExpression();
    return astContext_.create<ExpressionPattern>(expression,
                                                 makeRange(token, tokens_.current()));
}

ConstructorPattern* Parser::parseConstructorPattern(const Token& constructorName) {
    std::vector<PatternBinding*> bindings;
    if (tokens_.expect(TokenKind::LParen, "'(' after constructor pattern name")) {
        while (!tokens_.is(TokenKind::RParen) && tokens_.current().kind != TokenKind::Eof) {
            if (!tokens_.is(TokenKind::Identifier)) {
                errorExpected("pattern binding name");
                break;
            }
            const Token bindingName = tokens_.consume();
            TypeReprNode* bindingType = nullptr;
            PatternNode* nestedPattern = nullptr;
            if (tokens_.is(TokenKind::LParen)) {
                nestedPattern = parseConstructorPattern(bindingName);
            } else if (tokens_.tryConsume(TokenKind::Colon)) {
                bindingType = parseTypeRepr();
            }
            bindings.push_back(astContext_.create<PatternBinding>(
                tokenText(bindingName), bindingType, nestedPattern,
                makeRange(bindingName, tokens_.current())));
            if (!tokens_.tryConsume(TokenKind::Comma)) break;
        }
        tokens_.expect(TokenKind::RParen, "')' after constructor pattern bindings");
    }
    SourceRange range;
    range.begin = constructorName.loc;
    const SourceLocation endLoc = tokens_.current().loc;
    range.length = endLoc >= constructorName.loc
        ? static_cast<std::uint32_t>(endLoc - constructorName.loc)
        : 0;
    return astContext_.create<ConstructorPattern>(tokenText(constructorName),
                                                  std::move(bindings), range);
}

CaseClause* Parser::parseCaseClause() {
    const Token start = tokens_.current();
    std::vector<PatternNode*> patterns;
    bool isDefault = false;

    if (tokens_.isContextual(ContextualKeywordID::Bawaan)) {
        tokens_.consume();
        isDefault = true;
        tokens_.expect(TokenKind::Colon, "':' after bawaan");
    } else {
        tokens_.expect(TokenKind::KwKasus, "'kasus' in pilih statement");
        patterns.push_back(parseCasePattern());
        tokens_.expect(TokenKind::Colon, "':' after case pattern");
        while (tokens_.is(TokenKind::KwKasus)) {
            tokens_.consume();
            patterns.push_back(parseCasePattern());
            tokens_.expect(TokenKind::Colon, "':' after case pattern");
        }
    }

    CompoundStmt* body = parseCaseBody();
    return astContext_.create<CaseClause>(isDefault, std::move(patterns), body,
                                          makeRange(start, tokens_.current()));
}

PilihStmt* Parser::parsePilihStatement() {
    const Token keyword = tokens_.consume();
    ExprNode* subject = parseExpression();
    tokens_.expect(TokenKind::LBrace, "'{' after pilih subject");

    std::vector<CaseClause*> clauses;
    while (!tokens_.is(TokenKind::RBrace) && tokens_.current().kind != TokenKind::Eof) {
        if (tokens_.is(TokenKind::KwKasus) || tokens_.isContextual(ContextualKeywordID::Bawaan)) {
            clauses.push_back(parseCaseClause());
            continue;
        }
        errorExpected("case or bawaan in pilih statement");
        tokens_.consume();
    }

    const Token rbrace = tokens_.current();
    tokens_.expect(TokenKind::RBrace, "'}' to close pilih statement");
    return astContext_.create<PilihStmt>(subject, std::move(clauses),
                                         makeRange(keyword, rbrace));
}


GotoStmt* Parser::parseGotoStatement() {
    const Token keyword = tokens_.consume(); // goto contextual identifier
    if (!tokens_.is(TokenKind::Identifier)) {
        errorExpected("goto label");
        return astContext_.create<GotoStmt>("", makeRange(keyword));
    }
    const Token label = tokens_.consume();
    return astContext_.create<GotoStmt>(tokenText(label), makeRange(keyword, label));
}

LabelStmt* Parser::parseLabelStatement(const Token& labelName) {
    tokens_.expect(TokenKind::Colon, "':' after label");
    StmtNode* statement = parseStatement();
    return astContext_.create<LabelStmt>(tokenText(labelName), statement,
                                         makeRange(labelName, tokens_.current()));
}

JaminStmt* Parser::parseJaminStatement() {
    const Token keyword = tokens_.consume();
    tokens_.tryConsumeContextual(ContextualKeywordID::Saat);
    tokens_.tryConsumeContextual(ContextualKeywordID::Keluar);
    StmtNode* body = parseStatement();
    return astContext_.create<JaminStmt>(body, makeRange(keyword, tokens_.current()));
}


KeluarStmt* Parser::parseKeluarStatement() {
    const Token keyword = tokens_.consume();
    ExprNode* value = nullptr;
    if (!tokens_.is(TokenKind::RBrace) && !tokens_.is(TokenKind::Semi) &&
        tokens_.current().kind != TokenKind::Eof &&
        tokens_.current().kind != TokenKind::KwKasus &&
        !tokens_.isContextual(ContextualKeywordID::Bawaan)) {
        value = parseExpression();
    }
    return astContext_.create<KeluarStmt>(value, makeRange(keyword, tokens_.current()));
}

ReturnStmt* Parser::parseReturnStatement() {
    const Token keyword = tokens_.consume();
    ExprNode* value = nullptr;
    if (!tokens_.is(TokenKind::RBrace) && !tokens_.is(TokenKind::Semi) &&
        tokens_.current().kind != TokenKind::Eof) {
        value = parseExpression();
    }
    tokens_.tryConsume(TokenKind::Semi);
    return astContext_.create<ReturnStmt>(value, makeRange(keyword, tokens_.current()));
}

StmtNode* Parser::parseExpressionStatement() {
    const Token start = tokens_.current();
    ExprNode* expression = parseExpression();
    tokens_.tryConsume(TokenKind::Semi);
    return astContext_.create<ExprStmt>(expression, makeRange(start, tokens_.current()));
}

} // namespace rhc
