#include "RhLexer/KeywordTable.h"

namespace rhc {

namespace {
const KeywordEntry keywords[] = {
    {"Raw", TokenKind::Identifier, ContextualKeywordID::Raw},
    {"asinkron", TokenKind::KwAsinkron, ContextualKeywordID::None},
    {"atau", TokenKind::KwAtau, ContextualKeywordID::None},
    {"await", TokenKind::KwAwait, ContextualKeywordID::None},
    {"bawaan", TokenKind::Identifier, ContextualKeywordID::Bawaan},
    {"benar", TokenKind::KwBenar, ContextualKeywordID::None},
    {"berhenti", TokenKind::KwBerhenti, ContextualKeywordID::None},
    {"bilangan", TokenKind::KwBilangan, ContextualKeywordID::None},
    {"bilangan16", TokenKind::KwBilangan16, ContextualKeywordID::None},
    {"bilangan32", TokenKind::KwBilangan32, ContextualKeywordID::None},
    {"bilangan64", TokenKind::KwBilangan64, ContextualKeywordID::None},
    {"bilangan8", TokenKind::KwBilangan8, ContextualKeywordID::None},
    {"bilangan_kompleks", TokenKind::KwBilanganKompleks, ContextualKeywordID::None},
    {"bilangan_tanpa_batas", TokenKind::KwBilanganTanpaBatas, ContextualKeywordID::None},
    {"bilangan_tanpa_tanda16", TokenKind::KwBilanganTanpaTanda16, ContextualKeywordID::None},
    {"bilangan_tanpa_tanda32", TokenKind::KwBilanganTanpaTanda32, ContextualKeywordID::None},
    {"bilangan_tanpa_tanda64", TokenKind::KwBilanganTanpaTanda64, ContextualKeywordID::None},
    {"bilangan_tanpa_tanda8", TokenKind::KwBilanganTanpaTanda8, ContextualKeywordID::None},
    {"bukan", TokenKind::Identifier, ContextualKeywordID::Bukan},
    {"cacahan", TokenKind::KwCacahan, ContextualKeywordID::None},
    {"coroutine", TokenKind::Identifier, ContextualKeywordID::Coroutine},
    {"dalam", TokenKind::Identifier, ContextualKeywordID::Dalam},
    {"dan", TokenKind::KwDan, ContextualKeywordID::None},
    {"dari", TokenKind::KwDari, ContextualKeywordID::None},
    {"deklarasi", TokenKind::KwDeklarasi, ContextualKeywordID::None},
    {"dengan", TokenKind::Identifier, ContextualKeywordID::Dengan},
    {"desimal", TokenKind::KwDesimal, ContextualKeywordID::None},
    {"desimal_tetap", TokenKind::KwDesimalTetap, ContextualKeywordID::None},
    {"diatas", TokenKind::Identifier, ContextualKeywordID::Diatas},
    {"dimana", TokenKind::KwDimana, ContextualKeywordID::None},
    {"fungsi", TokenKind::KwFungsi, ContextualKeywordID::None},
    {"generik", TokenKind::KwGenerik, ContextualKeywordID::None},
    {"hampa", TokenKind::KwHampa, ContextualKeywordID::None},
    {"itu", TokenKind::Identifier, ContextualKeywordID::Itu},
    {"jamin", TokenKind::KwJamin, ContextualKeywordID::None},
    {"jeda", TokenKind::KwJeda, ContextualKeywordID::None},
    {"jika", TokenKind::KwJika, ContextualKeywordID::None},
    {"karakter", TokenKind::KwKarakter, ContextualKeywordID::None},
    {"kasus", TokenKind::KwKasus, ContextualKeywordID::None},
    {"ke", TokenKind::Identifier, ContextualKeywordID::Ke},
    {"keluar", TokenKind::Identifier, ContextualKeywordID::Keluar},
    {"kembalikan", TokenKind::KwKembalikan, ContextualKeywordID::None},
    {"kosong", TokenKind::KwKosong, ContextualKeywordID::None},
    {"lambda", TokenKind::KwLambda, ContextualKeywordID::None},
    {"langkah", TokenKind::Identifier, ContextualKeywordID::Langkah},
    {"lanjut", TokenKind::KwLanjut, ContextualKeywordID::None},
    {"larik", TokenKind::KwLarik, ContextualKeywordID::None},
    {"logika", TokenKind::KwLogika, ContextualKeywordID::None},
    {"maka", TokenKind::KwMaka, ContextualKeywordID::None},
    {"matriks", TokenKind::KwMatriks, ContextualKeywordID::None},
    {"pembangkit", TokenKind::Identifier, ContextualKeywordID::Pembangkit},
    {"pilih", TokenKind::KwPilih, ContextualKeywordID::None},
    {"prosedur", TokenKind::KwProsedur, ContextualKeywordID::None},
    {"prototipe", TokenKind::KwPrototipe, ContextualKeywordID::None},
    {"saat", TokenKind::Identifier, ContextualKeywordID::Saat},
    {"salah", TokenKind::KwSalah, ContextualKeywordID::None},
    {"sampai", TokenKind::KwSampai, ContextualKeywordID::None},
    {"selain", TokenKind::Identifier, ContextualKeywordID::Selain},
    {"selama", TokenKind::KwSelama, ContextualKeywordID::None},
    {"semua", TokenKind::Identifier, ContextualKeywordID::Semua},
    {"setiap", TokenKind::Identifier, ContextualKeywordID::Setiap},
    {"struktur", TokenKind::KwStruktur, ContextualKeywordID::None},
    {"teks", TokenKind::KwTeks, ContextualKeywordID::None},
    {"tetap", TokenKind::KwTetap, ContextualKeywordID::None},
    {"tugas_global", TokenKind::KwTugasGlobal, ContextualKeywordID::None},
    {"tugas_lokal", TokenKind::KwTugasLokal, ContextualKeywordID::None},
    {"tunjuk", TokenKind::KwTunjuk, ContextualKeywordID::None},
    {"ulangi", TokenKind::KwUlangi, ContextualKeywordID::None},
    {"untuk", TokenKind::KwUntuk, ContextualKeywordID::None},
    {"variabel", TokenKind::KwVariabel, ContextualKeywordID::None},
};
}

const KeywordEntry* KeywordTable::lookup(std::string_view name) {
    // Table is sorted by name; binary search keeps lookup deterministic and cheap.
    std::size_t lo = 0;
    std::size_t hi = sizeof(keywords) / sizeof(keywords[0]);
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const auto entryName = keywords[mid].name;
        if (entryName < name) {
            lo = mid + 1;
        } else if (name < entryName) {
            hi = mid;
        } else {
            return &keywords[mid];
        }
    }
    return nullptr;
}

} // namespace rhc
