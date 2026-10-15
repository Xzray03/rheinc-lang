#include "RhLexer/TokenKind.h"

namespace rhc {

const char* tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::Unknown: return "unknown";
    case TokenKind::Invalid: return "invalid";
    case TokenKind::Eof: return "eof";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::NumericConstant: return "numeric_constant";
    case TokenKind::StringLiteral: return "string_literal";
    case TokenKind::CharConstant: return "char_constant";
    case TokenKind::EndOfDirective: return "end_of_directive";

    case TokenKind::KwFungsi: return "kw_fungsi";
    case TokenKind::KwProsedur: return "kw_prosedur";
    case TokenKind::KwVariabel: return "kw_variabel";
    case TokenKind::KwTetap: return "kw_tetap";
    case TokenKind::KwDeklarasi: return "kw_deklarasi";
    case TokenKind::KwPrototipe: return "kw_prototipe";
    case TokenKind::KwKembalikan: return "kw_kembalikan";
    case TokenKind::KwStruktur: return "kw_struktur";
    case TokenKind::KwCacahan: return "kw_cacahan";
    case TokenKind::KwGenerik: return "kw_generik";
    case TokenKind::KwDimana: return "kw_dimana";
    case TokenKind::KwLambda: return "kw_lambda";

    case TokenKind::KwJika: return "kw_jika";
    case TokenKind::KwMaka: return "kw_maka";
    case TokenKind::KwUntuk: return "kw_untuk";
    case TokenKind::KwDari: return "kw_dari";
    case TokenKind::KwSampai: return "kw_sampai";
    case TokenKind::KwSelama: return "kw_selama";
    case TokenKind::KwUlangi: return "kw_ulangi";
    case TokenKind::KwBerhenti: return "kw_berhenti";
    case TokenKind::KwLanjut: return "kw_lanjut";
    case TokenKind::KwPilih: return "kw_pilih";
    case TokenKind::KwKasus: return "kw_kasus";
    case TokenKind::KwDan: return "kw_dan";
    case TokenKind::KwAtau: return "kw_atau";

    case TokenKind::KwAsinkron: return "kw_asinkron";
    case TokenKind::KwAwait: return "kw_await";
    case TokenKind::KwJeda: return "kw_jeda";
    case TokenKind::KwJamin: return "kw_jamin";
    case TokenKind::KwTugasGlobal: return "kw_tugas_global";
    case TokenKind::KwTugasLokal: return "kw_tugas_lokal";

    case TokenKind::KwBilangan: return "kw_bilangan";
    case TokenKind::KwBilangan8: return "kw_bilangan8";
    case TokenKind::KwBilangan16: return "kw_bilangan16";
    case TokenKind::KwBilangan32: return "kw_bilangan32";
    case TokenKind::KwBilangan64: return "kw_bilangan64";
    case TokenKind::KwBilanganTanpaTanda8: return "kw_bilangan_tanpa_tanda8";
    case TokenKind::KwBilanganTanpaTanda16: return "kw_bilangan_tanpa_tanda16";
    case TokenKind::KwBilanganTanpaTanda32: return "kw_bilangan_tanpa_tanda32";
    case TokenKind::KwBilanganTanpaTanda64: return "kw_bilangan_tanpa_tanda64";
    case TokenKind::KwDesimal: return "kw_desimal";
    case TokenKind::KwDesimalTetap: return "kw_desimal_tetap";
    case TokenKind::KwBilanganTanpaBatas: return "kw_bilangan_tanpa_batas";
    case TokenKind::KwBilanganKompleks: return "kw_bilangan_kompleks";
    case TokenKind::KwLogika: return "kw_logika";
    case TokenKind::KwKarakter: return "kw_karakter";
    case TokenKind::KwTeks: return "kw_teks";
    case TokenKind::KwHampa: return "kw_hampa";
    case TokenKind::KwLarik: return "kw_larik";
    case TokenKind::KwTunjuk: return "kw_tunjuk";
    case TokenKind::KwMatriks: return "kw_matriks";

    case TokenKind::KwBenar: return "kw_benar";
    case TokenKind::KwSalah: return "kw_salah";
    case TokenKind::KwKosong: return "kw_kosong";

    case TokenKind::LParen: return "l_paren";
    case TokenKind::RParen: return "r_paren";
    case TokenKind::LBrace: return "l_brace";
    case TokenKind::RBrace: return "r_brace";
    case TokenKind::LBracket: return "l_bracket";
    case TokenKind::RBracket: return "r_bracket";
    case TokenKind::Comma: return "comma";
    case TokenKind::Colon: return "colon";
    case TokenKind::Semi: return "semi";
    case TokenKind::Period: return "period";
    case TokenKind::Question: return "question";

    case TokenKind::Plus: return "plus";
    case TokenKind::Minus: return "minus";
    case TokenKind::Star: return "star";
    case TokenKind::Slash: return "slash";
    case TokenKind::Percent: return "percent";
    case TokenKind::StarStar: return "star_star";
    case TokenKind::Caret: return "caret";
    case TokenKind::Tilde: return "tilde";
    case TokenKind::PlusPlus: return "plus_plus";
    case TokenKind::MinusMinus: return "minus_minus";
    case TokenKind::SlashSlash: return "slash_slash";

    case TokenKind::Equal: return "equal";
    case TokenKind::PlusEqual: return "plus_equal";
    case TokenKind::MinusEqual: return "minus_equal";
    case TokenKind::StarEqual: return "star_equal";
    case TokenKind::SlashEqual: return "slash_equal";
    case TokenKind::PercentEqual: return "percent_equal";
    case TokenKind::StarStarEqual: return "star_star_equal";
    case TokenKind::SlashSlashEqual: return "slash_slash_equal";
    case TokenKind::LessLessEqual: return "less_less_equal";
    case TokenKind::GreaterGreaterEqual: return "greater_greater_equal";
    case TokenKind::AmpEqual: return "amp_equal";
    case TokenKind::PipeEqual: return "pipe_equal";
    case TokenKind::CaretEqual: return "caret_equal";

    case TokenKind::EqualEqual: return "equal_equal";
    case TokenKind::BangEqual: return "bang_equal";
    case TokenKind::Less: return "less";
    case TokenKind::Greater: return "greater";
    case TokenKind::LessEqual: return "less_equal";
    case TokenKind::GreaterEqual: return "greater_equal";

    case TokenKind::Amp: return "amp";
    case TokenKind::Pipe: return "pipe";
    case TokenKind::At: return "at";
    case TokenKind::LessLess: return "less_less";
    case TokenKind::GreaterGreater: return "greater_greater";

    case TokenKind::Arrow: return "arrow";
    case TokenKind::PlusGreater: return "plus_greater";

    case TokenKind::Hash: return "hash";
    case TokenKind::HashHash: return "hash_hash";
    case TokenKind::Backslash: return "backslash";
    }
    return "unknown";
}

} // namespace rhc
