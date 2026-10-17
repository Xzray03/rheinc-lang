#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// LoopInvariantCodeMotion (LICM): mengangkat instruksi loop-invariant
// murni dari body loop ke preheader.
//
// Loop: natural loop dari back-edge (header mendominasi latch); loop
// dengan header sama digabung. Preheader wajib predesessor luar tunggal
// header yang succ-nya hanya header; loop tanpa preheader seperti itu
// DILEWATI (splitting preheader butuh primitif block-baru, dirancang
// terpisah).
//
// Invarian (fixpoint): semua operand berasal dari luar loop (param,
// konstanta, atau def di block luar loop) atau def in-loop yang sudah
// terbukti invarian.
//
// Syarat hoist (disiplin speculatable): op murni DAN — bila op bisa
// trap pada operand ekstrem (Div/Mod/DivFloor/Pow: pembagian nol),
// block instruksi WAJIB mendominasi semua exit loop (penghitungan
// tidak boleh terlewat di jalur yang awalnya mengeksekusinya). Op lain
// (Add/Mul/bitwise/shift/cmp/cast dsb.) aman di-speculate ke preheader.
//
// Mekanik index-preserving: salinan instruksi disisipkan di ujung
// preheader (sebelum terminator) via insertInstrBefore, semua USE
// dialihkan ke value baru, instruksi lama disapu removeDeadInstrs.
// Loop diproses innermost-dulu (ukuran ascending); pass manager
// (fixpoint) mengulang bila pengangkatan membuka invarian baru.
class LoopInvariantCodeMotion final : public IrPass {
public:
    std::string_view name() const override { return "licm"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
