#include <algorithm>
#include "RhParser/Parser.h"

namespace rhc {
namespace {

int binaryPrecedence(TokenKind kind) {
    switch (kind) {
    case TokenKind::PlusGreater: return 5;

    case TokenKind::KwAtau: return 10;
    case TokenKind::KwDan: return 20;

    case TokenKind::Pipe: return 30;
    case TokenKind::Caret: return 35;
    case TokenKind::Amp: return 40;

    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
        return 50;

    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
        return 60;

    case TokenKind::LessLess:
    case TokenKind::GreaterGreater:
        return 70;

    case TokenKind::Plus:
    case TokenKind::Minus:
        return 80;

    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::SlashSlash:
    case TokenKind::Percent:
    case TokenKind::At:
        return 90;

    case TokenKind::StarStar:
        return 95;

    default:
        return -1;
    }
}

bool isRightAssociative(TokenKind kind) {
    return kind == TokenKind::StarStar;
}

UnaryOp unaryOpFromToken(TokenKind kind) {
    switch (kind) {
    case TokenKind::Minus: return UnaryOp::Minus;
    case TokenKind::Tilde: return UnaryOp::BitNot;
    case TokenKind::PlusPlus: return UnaryOp::Increment;
    case TokenKind::MinusMinus: return UnaryOp::Decrement;
    case TokenKind::KwAwait: return UnaryOp::Await;
    default: return UnaryOp::Plus;
    }
}

bool isAssignmentOperator(TokenKind kind) {
    switch (kind) {
    case TokenKind::Equal:
    case TokenKind::PlusEqual:
    case TokenKind::MinusEqual:
    case TokenKind::StarEqual:
    case TokenKind::SlashEqual:
    case TokenKind::SlashSlashEqual:
    case TokenKind::PercentEqual:
    case TokenKind::StarStarEqual:
    case TokenKind::LessLessEqual:
    case TokenKind::GreaterGreaterEqual:
    case TokenKind::AmpEqual:
    case TokenKind::PipeEqual:
    case TokenKind::CaretEqual:
        return true;
    default:
        return false;
    }
}

} // namespace

ExprNode* Parser::parseExpression() {
    return parseAssignmentExpression();
}

ExprNode* Parser::parseAssignmentExpression() {
    ExprNode* lhs = parseTernaryExpression();
    if (!isAssignmentOperator(tokens_.current().kind)) {
        return lhs;
    }

    const Token op = tokens_.consume();
    ExprNode* rhs = parseAssignmentExpression();
    return astContext_.create<BinaryExpr>(lhs, op.kind, rhs, makeRange(tokens_.current()));
}

ExprNode* Parser::parseTernaryExpression() {
    ExprNode* condition = parseBinaryExpression(1);
    if (!tokens_.tryConsume(TokenKind::Question)) {
        return condition;
    }

    ExprNode* trueValue = parseExpression();
    tokens_.expect(TokenKind::Colon, "':' in ternary expression");
    ExprNode* falseValue = parseTernaryExpression();
    return astContext_.create<TernaryExpr>(condition, trueValue, falseValue,
                                           makeRange(tokens_.current()));
}

ExprNode* Parser::parseBinaryExpression(int minPrecedence) {
    ExprNode* lhs = parseUnaryExpression();
    while (true) {
        const Token op = tokens_.current();
        const int precedence = binaryPrecedence(op.kind);
        if (precedence < minPrecedence) break;

        tokens_.consume();
        const int nextPrecedence = isRightAssociative(op.kind) ? precedence : precedence + 1;
        ExprNode* rhs = parseBinaryExpression(nextPrecedence);
        lhs = astContext_.create<BinaryExpr>(lhs, op.kind, rhs, makeRange(op));
    }
    return lhs;
}

ExprNode* Parser::parseUnaryExpression() {
    const Token token = tokens_.current();
    if (token.kind == TokenKind::Identifier && token.ident &&
        token.ident->getContextualKeywordID() == ContextualKeywordID::Ke) {
        return parsePostfixExpression();
    }
    if (token.kind == TokenKind::Identifier && tokenText(token) == "alamat") {
        const Token op = tokens_.consume();
        return astContext_.create<UnaryExpr>(UnaryOp::AddressOf, parseUnaryExpression(), false,
                                             makeRange(op));
    }
    if (token.kind == TokenKind::Identifier && token.ident &&
        token.ident->getContextualKeywordID() == ContextualKeywordID::Bukan) {
        const Token op = tokens_.consume();
        // `bukan` binds looser than relational: bukan x > y == bukan (x > y).
        return astContext_.create<UnaryExpr>(UnaryOp::LogicalNot, parseBinaryExpression(60), false,
                                             makeRange(op));
    }
    switch (token.kind) {
    case TokenKind::Minus:
    case TokenKind::Plus:
    case TokenKind::Tilde:
    case TokenKind::PlusPlus:
    case TokenKind::MinusMinus:
        tokens_.consume();
        return astContext_.create<UnaryExpr>(unaryOpFromToken(token.kind), parseUnaryExpression(),
                                             false, makeRange(token));
    case TokenKind::KwAwait: {
        tokens_.consume();
        ExprNode* operand = parseUnaryExpression();
        // Await result annotation: await http_get(url) -> hasil
        if (tokens_.tryConsume(TokenKind::Arrow) && tokens_.is(TokenKind::Identifier)) {
            tokens_.consume();
        }
        return astContext_.create<UnaryExpr>(UnaryOp::Await, operand, false, makeRange(token));
    }
    default:
        return parsePostfixExpression();
    }
}

ExprNode* Parser::parsePostfixExpression() {
    ExprNode* expression = parsePrimaryExpression();
    while (true) {
        const Token token = tokens_.current();
        // Bahasa ini tidak memakai titik koma: `++`/`--` di awal baris memulai statement
        // baru (prefix), bukan postfix dari ekspresi pada baris sebelumnya.
        if ((token.kind == TokenKind::PlusPlus || token.kind == TokenKind::MinusMinus) &&
            !hasFlag(token.flags, TokenFlags::StartOfLine)) {
            tokens_.consume();
            expression = astContext_.create<UnaryExpr>(unaryOpFromToken(token.kind), expression,
                                                       true, makeRange(token));
            continue;
        }
        if (token.kind == TokenKind::LBracket) {
            tokens_.consume();
            ExprNode* firstIndex = parseSliceArgument();
            ExprNode* index = firstIndex;
            if (tokens_.is(TokenKind::Comma)) {
                std::vector<ExprNode*> indices;
                indices.push_back(firstIndex);
                while (tokens_.tryConsume(TokenKind::Comma)) {
                    indices.push_back(parseSliceArgument());
                }
                index = astContext_.create<ArrayLiteralExpr>(std::move(indices),
                                                            makeRange(token));
            }
            const Token rbracket = tokens_.current();
            tokens_.expect(TokenKind::RBracket, "']' after index expression");
            SourceRange range;
            range.begin = expression->range.begin;
            const SourceLocation endLoc = rbracket.loc + rbracket.length;
            range.length = endLoc >= range.begin
                ? static_cast<std::uint32_t>(endLoc - range.begin)
                : 0;
            expression = astContext_.create<ArrayIndexExpr>(expression, index, range);
            continue;
        }
        if (token.kind == TokenKind::LParen) {
            tokens_.consume();
            SourceRange callStart;
            callStart.begin = expression->range.begin;
            expression = parseCallExpression(expression, callStart);
            continue;
        }
        if (token.kind == TokenKind::Period || token.kind == TokenKind::Arrow) {
            tokens_.consume();
            if (!tokens_.is(TokenKind::Identifier)) {
                errorExpected("member name after '.' or '->'");
                break;
            }
            const Token member = tokens_.consume();
            SourceRange range;
            range.begin = expression->range.begin;
            const SourceLocation endLoc = member.loc + member.length;
            range.length = endLoc >= range.begin
                ? static_cast<std::uint32_t>(endLoc - range.begin)
                : 0;
            expression = astContext_.create<MemberExpr>(expression, tokenText(member),
                                                        token.kind == TokenKind::Arrow, range);
            continue;
        }
        break;
    }
    return expression;
}


ExprNode* Parser::parseSliceArgument() {
    ExprNode* start = parseExpression();
    const bool hasSampai = tokens_.is(TokenKind::KwSampai);
    const bool hasLangkah = tokens_.is(TokenKind::Identifier) && tokens_.current().ident &&
                            tokens_.current().ident->getContextualKeywordID() == ContextualKeywordID::Langkah;
    if (!hasSampai && !hasLangkah) {
        if (start->kind == AstKind::IdentifierExpr && tokens_.is(TokenKind::Identifier) &&
            tokens_.current().ident &&
            tokens_.current().ident->getContextualKeywordID() == ContextualKeywordID::Ke &&
            tokens_.peek().kind == TokenKind::Identifier) {
            tokens_.consume(); // ke
            const Token tail = tokens_.consume();
            const std::string text = static_cast<IdentifierExpr*>(start)->name + " ke " + tokenText(tail);
            return astContext_.create<IdentifierExpr>(text, makeRange(tokens_.current()));
        }
        return start;
    }

    tokens_.tryConsume(TokenKind::KwSampai);
    ExprNode* end = nullptr;
    if (!tokens_.is(TokenKind::RBracket) && !tokens_.is(TokenKind::RParen) &&
        !tokens_.is(TokenKind::Comma) && !hasLangkah) {
        end = parseExpression();
    }
    ExprNode* step = nullptr;
    if (tokens_.is(TokenKind::Identifier) && tokens_.current().ident &&
        tokens_.current().ident->getContextualKeywordID() == ContextualKeywordID::Langkah) {
        tokens_.consume();
        step = parseExpression();
    }
    SourceRange range;
    range.begin = start->range.begin;
    const SourceLocation endLoc = tokens_.current().loc;
    range.length = endLoc >= range.begin
        ? static_cast<std::uint32_t>(endLoc - range.begin)
        : 0;
    return astContext_.create<SliceExpr>(start, end, step, range);
}


ExprNode* Parser::parseTypeExpression() {
    const Token start = tokens_.current();
    TypeReprNode* type = parseTypeRepr();
    SourceRange range;
    range.begin = start.loc;
    const SourceLocation endLoc = tokens_.current().loc;
    range.length = endLoc >= start.loc
        ? static_cast<std::uint32_t>(endLoc - start.loc)
        : 0;
    return astContext_.create<TypeExpr>(type, range);
}

ExprNode* Parser::parsePrimaryExpression() {
    const Token token = tokens_.current();

    if ((token.kind == TokenKind::KwDesimalTetap ||
         token.kind == TokenKind::KwBilanganKompleks ||
         token.kind == TokenKind::KwMatriks) &&
        tokens_.peek().kind == TokenKind::LParen) {
        tokens_.consume();
        tokens_.consume(); // consume the peeked '(' token too
        ExprNode* callee = astContext_.create<IdentifierExpr>(tokenText(token), makeRange(token));
        return parseCallExpression(callee, makeRange(token));
    }

    switch (token.kind) {
    case TokenKind::NumericConstant: {
        tokens_.consume();
        return astContext_.create<IntegerLiteralExpr>(tokenText(token), makeRange(token));
    }
    case TokenKind::CharConstant: {
        tokens_.consume();
        return astContext_.create<CharLiteralExpr>(tokenText(token), makeRange(token));
    }
    case TokenKind::StringLiteral: {
        tokens_.consume();
        const bool raw = hasFlag(token.flags, TokenFlags::RawLiteral);
        return astContext_.create<StringLiteralExpr>(tokenText(token), raw, makeRange(token));
    }
    case TokenKind::KwJika: {
        const Token keyword = tokens_.consume();
        ExprNode* condition = parseExpression();
        tokens_.expect(TokenKind::KwMaka, "'maka' in ternary expression");
        ExprNode* trueValue = parseExpression();
        tokens_.tryConsumeContextual(ContextualKeywordID::Selain);
        ExprNode* falseValue = parseExpression();
        return astContext_.create<TernaryExpr>(condition, trueValue, falseValue,
                                               makeRange(keyword, tokens_.current()));
    }
    case TokenKind::KwBenar:
        tokens_.consume();
        return astContext_.create<BoolLiteralExpr>(true, makeRange(token));
    case TokenKind::KwSalah:
        tokens_.consume();
        return astContext_.create<BoolLiteralExpr>(false, makeRange(token));
    case TokenKind::KwKosong:
        tokens_.consume();
        return astContext_.create<NullLiteralExpr>(makeRange(token));
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
    case TokenKind::KwLarik:
    case TokenKind::KwTunjuk:
    case TokenKind::KwMatriks:
        return parseTypeExpression();
    case TokenKind::KwLambda: {
        const Token lambdaToken = tokens_.consume();
        std::vector<std::string> typeParameters;
        TraitConstraintClauses lambdaConstraints;
        if (tokens_.is(TokenKind::LParen) && tokens_.peek().kind == TokenKind::KwGenerik) {
            tokens_.consume(); // (
            tokens_.consume(); // generik
            while (tokens_.is(TokenKind::Identifier)) {
                typeParameters.push_back(tokenText(tokens_.consume()));
                if (tokens_.is(TokenKind::Colon)) {
                    tokens_.consume();
                    if (tokens_.is(TokenKind::Identifier)) {
                        std::string tv = typeParameters.back();
                        std::string trait = tokenText(tokens_.consume());
                        auto it = std::find_if(lambdaConstraints.begin(),
                                               lambdaConstraints.end(),
                                               [&](const auto& c) { return c.first == tv; });
                        if (it == lambdaConstraints.end()) {
                            lambdaConstraints.emplace_back(
                                tv, std::vector<std::string>{std::move(trait)});
                        } else {
                            it->second.push_back(std::move(trait));
                        }
                    }
                }
                if (!tokens_.tryConsume(TokenKind::Comma)) break;
            }
            tokens_.expect(TokenKind::RParen, "')' after lambda generic parameters");
        }
        std::vector<std::pair<std::string, TypeReprNode*>> parameters;
        if (tokens_.expect(TokenKind::LParen, "'(' after lambda")) {
            while (!tokens_.is(TokenKind::RParen) && tokens_.current().kind != TokenKind::Eof) {
                if (!tokens_.is(TokenKind::Identifier)) {
                    errorExpected("lambda parameter name");
                    break;
                }
                const Token name = tokens_.consume();
                TypeReprNode* type = nullptr;
                if (tokens_.tryConsume(TokenKind::Colon)) {
                    type = parseTypeRepr();
                }
                parameters.emplace_back(tokenText(name), type);
                if (!tokens_.tryConsume(TokenKind::Comma)) break;
            }
            tokens_.expect(TokenKind::RParen, "')' after lambda parameters");
        }
        TypeReprNode* returnType = nullptr;
        if (tokens_.tryConsume(TokenKind::Arrow)) {
            returnType = parseTypeRepr();
        }
        CompoundStmt* body = parseBlock();
        SourceRange range;
        range.begin = lambdaToken.loc;
        const SourceLocation endLoc = body->range.begin + body->range.length;
        range.length = endLoc >= lambdaToken.loc
            ? static_cast<std::uint32_t>(endLoc - lambdaToken.loc)
            : 0;
        auto* lambdaExpr = astContext_.create<LambdaExpr>(
            std::move(typeParameters), std::move(parameters), returnType, body, range);
        lambdaExpr->constraints = std::move(lambdaConstraints);
        return lambdaExpr;
    }
    case TokenKind::Identifier:
        if ((tokenText(token) == "Antrian" || tokenText(token) == "Tumpukan" ||
             tokenText(token) == "Daftar" || tokenText(token) == "Iterator") &&
            tokens_.peek().kind == TokenKind::Less) {
            const Token name = tokens_.consume();
            auto arguments = parseTypeArgumentList(true); // consumes '<' itself
            TypeReprNode* generic = astContext_.create<CallTypeRepr>(
                tokenText(name), std::move(arguments), true,
                makeRange(name, tokens_.current()));
            return astContext_.create<TypeExpr>(generic, makeRange(name, tokens_.current()));
            return astContext_.create<TypeExpr>(generic, makeRange(name, tokens_.current()));
        }
        return parseIdentifierOrCallExpression();
    case TokenKind::LParen: {
        const Token lparen = tokens_.consume();
        ExprNode* first = parseExpression();
        if (!tokens_.is(TokenKind::Comma)) {
            tokens_.expect(TokenKind::RParen, "')' after expression");
            return first;
        }
        std::vector<ExprNode*> elements;
        elements.push_back(first);
        while (tokens_.tryConsume(TokenKind::Comma)) {
            elements.push_back(parseExpression());
        }
        const Token rparen = tokens_.current();
        tokens_.expect(TokenKind::RParen, "')' after tuple expression");
        return astContext_.create<ArrayLiteralExpr>(std::move(elements),
                                                    makeRange(lparen, rparen));
    }
    case TokenKind::LBracket: {
        const Token lbracket = tokens_.consume();
        std::vector<ExprNode*> elements;
        while (!tokens_.is(TokenKind::RBracket) && tokens_.current().kind != TokenKind::Eof) {
            elements.push_back(parseExpression());
            if (!tokens_.tryConsume(TokenKind::Comma)) break;
        }
        const Token rbracket = tokens_.current();
        tokens_.expect(TokenKind::RBracket, "']' to close array literal");
        return astContext_.create<ArrayLiteralExpr>(std::move(elements),
                                                    makeRange(lbracket, rbracket));
    }
    case TokenKind::LBrace: {
        const Token lbrace = tokens_.consume();
        std::vector<ExprNode*> elements;
        while (!tokens_.is(TokenKind::RBrace) && tokens_.current().kind != TokenKind::Eof) {
            elements.push_back(parseExpression());
            if (!tokens_.tryConsume(TokenKind::Comma)) break;
        }
        const Token rbrace = tokens_.current();
        tokens_.expect(TokenKind::RBrace, "'}' to close array literal");
        return astContext_.create<ArrayLiteralExpr>(std::move(elements), makeRange(lbrace, rbrace));
    }
    default:
        errorExpected("expression");
        tokens_.consume();
        return astContext_.create<ErrorExpr>(makeRange(token));
    }
}

bool Parser::genericTypeAhead() {
    // tokens_.current() is '<'. Scan for the matching '>' and require a
    // call-argument terminator after it, so `a < b` stays a comparison.
    int angleDepth = 1;
    int parenDepth = 0;
    for (std::size_t n = 1; n < 64; ++n) {
        const TokenKind kind = tokens_.peek(n).kind;
        if (kind == TokenKind::Eof) return false;
        if (kind == TokenKind::LParen || kind == TokenKind::LBracket) ++parenDepth;
        if ((kind == TokenKind::RParen || kind == TokenKind::RBracket) && parenDepth > 0) {
            --parenDepth;
        }
        if (parenDepth == 0) {
            if (kind == TokenKind::Less) ++angleDepth;
            if (kind == TokenKind::Greater) {
                --angleDepth;
                if (angleDepth == 0) {
                    const TokenKind after = tokens_.peek(n + 1).kind;
                    return after == TokenKind::Comma || after == TokenKind::RParen ||
                           after == TokenKind::RBracket;
                }
            }
        }
    }
    return false;
}

ExprNode* Parser::parseIdentifierOrCallExpression() {
    TraitConstraintClauses lambdaConstraints;
    const Token identifier = tokens_.consume();
    ExprNode* callee = astContext_.create<IdentifierExpr>(tokenText(identifier), makeRange(identifier));

    // Expression-position generic type: alokasi(Antrian<bilangan>).
    if (tokens_.is(TokenKind::Less) && genericTypeAhead()) {
        auto arguments = parseTypeArgumentList(true); // consumes '<' itself
        TypeReprNode* generic = astContext_.create<CallTypeRepr>(
            tokenText(identifier), std::move(arguments), true,
            makeRange(identifier, tokens_.current()));
        return astContext_.create<TypeExpr>(generic, makeRange(identifier, tokens_.current()));
    }

    if (tokens_.tryConsume(TokenKind::LParen)) {
        return parseCallExpression(callee, makeRange(identifier));
    }

    const std::string calleeName = tokenText(identifier);
    if (calleeName == "hasil" && tokens_.is(TokenKind::KwDari)) {
        tokens_.consume(); // dari
        ExprNode* value = parseExpression();
        return astContext_.create<CallExpr>(callee, std::vector<ExprNode*>{value},
                                            makeRange(identifier));
    }

    // RheinC supports no-parentheses call style for selected variadic builtins such as
    // tampilkan "hello", name, "\n". Identifier followed by identifier must remain
    // statement boundary, not an implicit call.
    if (calleeName != "tampilkan") {
        return callee;
    }
    const bool canStartArgument =
        tokens_.is(TokenKind::StringLiteral) ||
        tokens_.is(TokenKind::NumericConstant) ||
        tokens_.is(TokenKind::Identifier) ||
        tokens_.is(TokenKind::LParen);
    if (!canStartArgument) {
        return callee;
    }

    std::vector<ExprNode*> arguments;
    if (tokens_.is(TokenKind::Identifier) && tokens_.peek().kind == TokenKind::Equal) {
        const Token name = tokens_.consume();
        tokens_.consume();
        arguments.push_back(astContext_.create<NamedArgumentExpr>(
            tokenText(name), parseExpression(), makeRange(name)));
    } else {
        arguments.push_back(parseSliceArgument());
    }
    while (tokens_.tryConsume(TokenKind::Comma)) {
        if (tokens_.isContextual(ContextualKeywordID::Dengan)) {
            tokens_.consume();
        }
        if (tokens_.is(TokenKind::Identifier) && tokens_.peek().kind == TokenKind::Equal) {
            const Token name = tokens_.consume();
            tokens_.consume();
            arguments.push_back(astContext_.create<NamedArgumentExpr>(
                tokenText(name), parseSliceArgument(), makeRange(name)));
        } else {
            arguments.push_back(parseSliceArgument());
        }
    }
    return astContext_.create<CallExpr>(callee, std::move(arguments), makeRange(identifier));
}

ExprNode* Parser::parseCallExpression(ExprNode* callee, SourceRange startRange) {
    std::vector<ExprNode*> arguments;
    while (!tokens_.is(TokenKind::RParen) && tokens_.current().kind != TokenKind::Eof) {
        if (tokens_.isContextual(ContextualKeywordID::Dengan)) {
            tokens_.consume();
        }
        if (tokens_.is(TokenKind::Identifier) && tokens_.peek().kind == TokenKind::Equal) {
            const Token name = tokens_.consume();
            tokens_.consume();
            arguments.push_back(astContext_.create<NamedArgumentExpr>(
                tokenText(name), parseSliceArgument(), makeRange(name)));
        } else {
            arguments.push_back(parseSliceArgument());
        }
        if (!tokens_.tryConsume(TokenKind::Comma)) break;
    }
    const Token rparen = tokens_.current();
    tokens_.expect(TokenKind::RParen, "')' after call arguments");

    SourceRange callRange;
    callRange.begin = startRange.begin;
    const SourceLocation endLoc = rparen.loc + rparen.length;
    callRange.length = endLoc >= startRange.begin
        ? static_cast<std::uint32_t>(endLoc - startRange.begin)
        : 0;
    return astContext_.create<CallExpr>(callee, std::move(arguments), callRange);
}

} // namespace rhc
