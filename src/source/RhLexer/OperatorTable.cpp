#include "RhLexer/OperatorTable.h"

namespace rhc {

namespace {
struct OperatorEntry {
    std::string_view spelling;
    TokenKind kind;
};

const OperatorEntry operators[] = {
    {"<<=", TokenKind::LessLessEqual},
    {">>=", TokenKind::GreaterGreaterEqual},
    {"**=", TokenKind::StarStarEqual},
    {"//=", TokenKind::SlashSlashEqual},
    {"**", TokenKind::StarStar},
    {"//", TokenKind::SlashSlash},
    {"++", TokenKind::PlusPlus},
    {"--", TokenKind::MinusMinus},
    {"->", TokenKind::Arrow},
    {"+>", TokenKind::PlusGreater},
    {"==", TokenKind::EqualEqual},
    {"!=", TokenKind::BangEqual},
    {"<=", TokenKind::LessEqual},
    {">=", TokenKind::GreaterEqual},
    {"<<", TokenKind::LessLess},
    {">>", TokenKind::GreaterGreater},
    {"+=", TokenKind::PlusEqual},
    {"-=", TokenKind::MinusEqual},
    {"*=", TokenKind::StarEqual},
    {"/=", TokenKind::SlashEqual},
    {"%=", TokenKind::PercentEqual},
    {"&=", TokenKind::AmpEqual},
    {"|=", TokenKind::PipeEqual},
    {"^=", TokenKind::CaretEqual},
    {"##", TokenKind::HashHash},
    {"(", TokenKind::LParen},
    {")", TokenKind::RParen},
    {"{", TokenKind::LBrace},
    {"}", TokenKind::RBrace},
    {"[", TokenKind::LBracket},
    {"]", TokenKind::RBracket},
    {",", TokenKind::Comma},
    {":", TokenKind::Colon},
    {";", TokenKind::Semi},
    {".", TokenKind::Period},
    {"?", TokenKind::Question},
    {"+", TokenKind::Plus},
    {"-", TokenKind::Minus},
    {"*", TokenKind::Star},
    {"/", TokenKind::Slash},
    {"%", TokenKind::Percent},
    {"^", TokenKind::Caret},
    {"~", TokenKind::Tilde},
    {"=", TokenKind::Equal},
    {"<", TokenKind::Less},
    {">", TokenKind::Greater},
    {"&", TokenKind::Amp},
    {"|", TokenKind::Pipe},
    {"@", TokenKind::At},
    {"#", TokenKind::Hash},
    {"\\", TokenKind::Backslash},
};
}

TokenKind OperatorTable::lookup(std::string_view text) {
    // Lexer tries candidate lengths from longest to shortest. A lookup is exact
    // for the candidate substring; maximal munch is handled by the caller.
    for (const OperatorEntry& entry : operators) {
        if (entry.spelling == text) {
            return entry.kind;
        }
    }
    return TokenKind::Unknown;
}

} // namespace rhc
