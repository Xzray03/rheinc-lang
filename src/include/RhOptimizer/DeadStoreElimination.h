#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// DeadStoreElimination (bentuk store coalescing yang relevan di IR ini):
// menghapus Store ke SSA-pointer NON-ESCAPE yang DITIMPA store berikutnya
// sebelum sempat terbaca. Store di IR Rheinc selalu whole-value (tak ada
// store partial byte-level), sehingga "coalescing" = eliminasi store
// yang ter-overwrite.
//
// Scope v1 (sesuai keputusan):
//   - Alamat SSA-same (ValueV identik = alamat sama; no alias analysis).
//   - Pointer NON-ESCAPE (dipakai hanya Store/Load alamat lokal) --
//     berbeda dengan store-forwarding: store yang dihapus harus benar-
//     benar tak terbaca pihak lain (escape/call/thread).
//   - Intra-block & urutan pasti: S1, S2 di block yang sama,
//     pos(S1) < pos(S2), tanpa Load[p] di antara, tanpa Call/CallIndirect
//     di antara (Call bisa membaca memori -- barrier konservatif).
//   - GlobalV: DILEWATI (race antar-thread tanpa analisis concurrency).
// Load[p] di antara dua store "menyelamatkan" store pertama (nilainya
// terbaca); store pertama lalu ditandai dead bila ditimpa lagi.
class DeadStoreElimination final : public IrPass {
public:
    std::string_view name() const override { return "dead-store-elim"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
