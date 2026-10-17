#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// StructLayoutOptimize: satu pass DFE + field reordering untuk tipe
// IrTypeKind::Struct:
//
//   1. GUARD per tipe: semua akses field (Extract/Insert) ke tipe ini
//      memakai idx KONSTAN (ImmV) -- idx ValueV membuat field yang
//      terbaca tak terdecide -> skip tipe. Tipe dengan bitfield
//      (bitWidth != 0) juga skip (packing byte bersama rapuh).
//   2. DFE: field HIDUP jika ada Extract idx i (baca) atau Insert idx i
//      (tulis) di seluruh modul. Field tanpa keduanya = MATI (ditulis
//      Construct ke slot tak terbaca -- sound karena Construct dipakai
//      translator untuk default value dan operand-nya tak terbaca).
//   3. REORDER: field hidup diurutkan by (alignBytes desc, sizeBytes
//      desc) -- System V packing optimal untuk urutan bebas.
//   4. SATU remap module-wide: operand idx ImmV Extract/Insert lewat
//      peta old->new; Construct (operands positional [TypeV, v...])
//      di-remap posisi value-nya; offsetBytes dihitung ulang sederhana
//      (LayoutEngine final tetap sumber kebenaran).
//
// Whole-value flow (Load/Store/Phi/param/ret) AMAN: value struct opaque,
// tidak menyentuh field. Invariant: idx ValueV / bitfield -> tidak ada
// transformasi sama sekali pada tipe itu.
class StructLayoutOptimize final : public IrPass {
public:
    std::string_view name() const override { return "struct-layout"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
