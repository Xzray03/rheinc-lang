#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// StaticVariableUnification (SVU kategori 1): mengunifikasi variabel
// global yang BENAR-BENAR STATIS -- di-Store TEPAT SATU KALI di
// .__global_init (dikonvensi FinalIr: init berjalan sebelum entry) dan
// TIDAK PERNAH di-Store lagi di fungsi mana pun -- dengan nilai init
// KONSTAN.
//
// Transformasi per global kandidat:
//   1. Semua Load @G di seluruh fungsi -> konstanta inline
//      (int/bool -> ImmV; desimal -> ConstantV ke pool FloatBits dengan
//      dedup). "Truth"-nya = satu representasi konstanta -- dua global
//      bernilai sama OTOMATIS menyatu ke ImmV/ConstantV yang sama.
//   2. Store init + Load sisa dihapus (eliminateInstrs).
//   3. Slot global dihapung total: globalVars dipadatkan + semua operand
//      GlobalV di-remap (IrEditor::removeDeadGlobals) -- variabel asli
//      lenyap termasuk namanya dari metadata.
//
// Global di-Store lebih dari sekali, di-Store di luar .__global_init
// dengan jumlah >1, atau init-nya tidak resolve konstan -> DILEWATI.
// Concurrency aman untuk read: tidak ada penulis ulang.
class StaticVariableUnification final : public IrPass {
public:
    std::string_view name() const override { return "static-var-unify"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
