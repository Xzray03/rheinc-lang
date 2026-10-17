#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// StoreForwarding: mem-forward Load dari alamat yang sama dengan Store
// yang mendominasinya -- untuk pointer SSA arbitrer (param/self) dan
// GlobalV. Melengkapi escape-analysis/scalar-replacement yang hanya
// menang Alloc non-escaping DAN mengeliminasi alokasi; pass ini TIDAK
// menuntut non-escape (memori tetap ada, hanya nilai Load di-forward)
// sehingga jauh lebih luas cakupannya -- khususnya method object
// (self-pointer read-after-write), pola paling umum di korpus.

// Dua kategori (satu pass):
//   1. SSA-same pointer: Store[p, v] -> Load[p] dengan p SSA value yang
//      SAMA (address identity by SSA -- tanpa alias analysis).
//   2. GlobalV: store @G -> load @G di FUNGSI YANG SAMA (intra-fungsi;
//      antar-fungsi butuh interprocedural availability -> fase lanjutan).
//
// Aturan sound (ketat, sama keluarga dengan scalar-replacement):
//   - Tepat SATU Store ke alamat itu di fungsi + Store-block mendominasi
//     Load-block -> forward; ATAU
//   - Multi-store: Load diganti value Store penampung S (S.blk didahului
//     semua store lain + dom(S, B) + semua pred B didominasi S & bebas
//     store ke alamat itu + posisi saat B == S.blk -> Load sesudah Store).
//   Store DIJAGA tetap ada (side effect / alamat bisa escape).
//   Load yang store-nya mungkin terlewat -> DILEWATI.
class StoreForwarding final : public IrPass {
public:
    std::string_view name() const override { return "store-forwarding"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
