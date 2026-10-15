#pragma once

#include <cstdint>

namespace rhc {

enum class TokenKind : std::uint16_t {
    Unknown,
    Invalid,
    Eof,

    Identifier,

    NumericConstant,
    StringLiteral,
    CharConstant,

    EndOfDirective,

    KwFungsi,
    KwProsedur,
    KwVariabel,
    KwTetap,
    KwDeklarasi,
    KwPrototipe,
    KwKembalikan,
    KwStruktur,
    KwCacahan,
    KwGenerik,
    KwDimana,
    KwLambda,

    KwJika,
    KwMaka,
    KwUntuk,
    KwDari,
    KwSampai,
    KwSelama,
    KwUlangi,
    KwBerhenti,
    KwLanjut,
    KwPilih,
    KwKasus,
    KwDan,
    KwAtau,

    KwAsinkron,
    KwAwait,
    KwJeda,
    KwJamin,
    KwTugasGlobal,
    KwTugasLokal,

    KwBilangan,
    KwBilangan8,
    KwBilangan16,
    KwBilangan32,
    KwBilangan64,
    KwBilanganTanpaTanda8,
    KwBilanganTanpaTanda16,
    KwBilanganTanpaTanda32,
    KwBilanganTanpaTanda64,
    KwDesimal,
    KwDesimalTetap,
    KwBilanganTanpaBatas,
    KwBilanganKompleks,
    KwLogika,
    KwKarakter,
    KwTeks,
    KwHampa,
    KwLarik,
    KwTunjuk,
    KwMatriks,

    KwBenar,
    KwSalah,
    KwKosong,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,

    Comma,
    Colon,
    Semi,
    Period,
    Question,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    StarStar,
    Caret,
    Tilde,
    PlusPlus,
    MinusMinus,
    SlashSlash,

    Equal,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    StarStarEqual,
    SlashSlashEqual,
    LessLessEqual,
    GreaterGreaterEqual,
    AmpEqual,
    PipeEqual,
    CaretEqual,

    EqualEqual,
    BangEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,

    Amp,
    Pipe,
    At,
    LessLess,
    GreaterGreater,

    Arrow,
    PlusGreater,

    Hash,
    HashHash,
    Backslash,
};

const char* tokenKindName(TokenKind kind);

} // namespace rhc
