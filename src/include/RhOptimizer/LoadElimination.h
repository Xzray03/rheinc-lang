#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// LoadElimination (redundant load elimination, cross-block dominasi):
// v2 = Load[p] diganti v1 = Load[p] yang sama bila nilai pasti identik
// -- menutup segitiga optimasi memori (Store->Load store-forwarding,
// Store->Store dead-store-elim, Load->Load pass ini).

// Sound untuk p SSA-same NON-ESAPE (dua Load terpisah waktu: pihak lain
// bisa menulis di antara -> non-escape wajib, tanpa analisis thread) dan:
//   1. dom(L1.block, L2.block)            (L1 sebelum L2 di semua jalur)
//   2. tidak ada Store[p] yang dieksekusi SETELAH L1 dan SEBELUM L2:
//      untuk tiap Store S[p]: INVALID bila reachable(S.blk, L2.blk) dan
//      reachable(L1.blk, S.blk), kecuali S.blk == L1.blk dengan
//      posisi S SEBELUM L1 (S sudah tercermin dalam nilai L1)
//   3. tidak ada Call/CallIndirect yang bisa berada di antara: cukup
//      dicek konservatif -- bila ada Call di fungsi ini, reachability
//      Call.blk -> L2.blk dan L1.blk -> Call.blk -> invalid (Call bisa
//      menulis *p lewat pointer lain -- pointer non-escape tidak lolos
//      ke Call, tapi Call tanpa argumen p tetap bisa membaca memori
//      global/heap yang ALIAS? tidak: p non-escape = hanya diakses lokal,
//      Call tidak menerima p, dan tidak ada jalan lain menulis *p ->
//      Call AMAN untuk p non-escape. Syarat 3 TIDAK dipakai; dok.)
// GlobalV: skip (race antar-thread).
class LoadElimination final : public IrPass {
public:
    std::string_view name() const override { return "load-elimination"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
