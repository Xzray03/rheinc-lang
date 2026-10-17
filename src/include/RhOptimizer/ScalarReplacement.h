#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// ScalarReplacement: mempromosikan Load dari alokasi `Alloc` non-escaping
// dengan BANYAK Store (versi multi-store dari escape analysis). Escape
// analysis menangani kasus tepat-satu-Store; pass ini menangani sisanya
// yang memenuhi aturan ketat (sound, loop-aman) berikut -- Load di block B
// diganti value Store S jika dan hanya jika:
//
//   1. dom(S.block, B)                    (S tercapai sebelum B), DAN
//   2. untuk semua Store S' lain: dom(S'.block, S.block)
//      (S.block didahului SEMUA store lain -> S dieksekusi SESUDAH store
//      lain mana pun di jalur manapun; S adalah "store penampung"), DAN
//   3. untuk semua pred langsung P dari B: dom(S.block, P) DAN P bebas
//      Store ke ptr (setiap pendekatan terakhir ke B melewati S tepat
//      sebelumnya; store di latch loop otomatis GAGAL syarat ini -> tidak
//      ada promosi load di loop yang store-nya di latch), DAN
//   4. bila B == S.block: posisi Load harus SESUDAH Store dalam urutan
//      block.
//
// Bila SEMUA Load dari ptr terpromosi (atau memang tak ada Load), Alloc +
// seluruh Store-nya dieliminasi (via IrEditor::eliminateInstrs) karena
// tidak escape dan tidak pernah dibaca.
//
// Pola yang GAGAL aturan (join ambiguity, store-di-loop-body, aliasing
// kompleks): DILEWATI -- konservatif penuh, tidak ada transformasi yang
// meragukan.
class ScalarReplacement final : public IrPass {
public:
    std::string_view name() const override { return "scalar-replacement"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
