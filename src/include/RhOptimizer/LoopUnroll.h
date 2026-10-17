#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// LoopUnroll: unrolling loop dengan pemilihan strategi per loop.
// DUA JALUR AKTIF:
//   - FULL UNROLLING: loop terhitung KONSTAN berbentuk ketat
//     (trip count statis 1..16) -> loop dihapus, body diklon N kali;
//   - PARTIAL UNROLLING (loop peeling K=2): loop berbentuk ketat dengan
//     batas DINAMIS -> 2 iterasi pertama diklon keluar sebagai guard
//     (clone head mengecek kondisi; bila false -> exit dengan nilai
//     carried yang benar lewat phi exit bernama "exit_peel"), loop asli
//     tetap hidup untuk sisa iterasi.
// Loop yang TIDAK memenuhi bentuk ketat (multi-exit, body bercabang,
// lanjutkan/berhenti kompleks, dsb.): DILEWATI -- IR tidak diubah,
// changed tidak diangkat. Runtime unrolling (unroll-by-K di dalam loop
// dengan remainder) SENGAJA tidak diimplementasikan.
//
// Syarat bentuk loop (ketat, selain itu dilewati):
//   - preheader tunggal (pred luar header yang succ-nya hanya header);
//   - single back-edge: latch tunggal L dengan succ == {header H};
//   - body LINEAR: rantai B0 -> B1 -> ... -> L, satu pred/succ internal;
//   - single exit: succ luar loop HANYA H -> E;
//   - trip count statis: terminator H berbentuk CondBr(CmpLt(phi_i, N))
//     dengan phi_i loop-carried bernilai awal ConstInt(0) dan update
//     Add(phi_i, 1) -- loop penghitung naif dari 0;
//   - batas: 1 <= N <= 16.
//
// Mekanik (index-preserving via IrEditor):
//   - tiap iterasi diklon: instruksi H non-phi + rantai body, operand
//     SSA di-remap (phi: iter-0 -> nilai init, iter-c -> clone update
//     iter-(c-1); def H/chain -> clone iter-c; eksternal -> sama);
//   - edge pre -> H dialihkan ke clone-0; clone terakhir -> E;
//   - preds/succs diperbaiki; exit phi di E di-remap ([H, v] ->
//     [latchCloneTerakhir, v@clone-N-1]);
//   - use phi/def-H di luar loop di-remap ke clone terakhir;
//   - loop lama (H + rantai) menjadi tak terjangkau -> disapu
//     removeUnreachableBlocks; DCE pass berikutnya membersihkan cmp mati.
class LoopUnroll final : public IrPass {
public:
    std::string_view name() const override { return "loop-unroll"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
