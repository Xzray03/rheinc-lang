#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// ConstantPropagation: mengganti operand USE yang merujuk SSA value
// konstan (ConstInt/ConstBool, termasuk Phi yang runtuh menjadi konstanta)
// menjadi ImmV langsung.
//
// Karena IR memakai SSA (def tunggal per value), nilai dari instruksi
// konstan adalah konstan di SEMUA titik pemakaian — propagasi cukup
// rewrite operand ValueV -> ImmV per fungsi, TANPA analisis dominasi.
//
// Cakupan:
//   - ConstInt/ConstBool sebagai sumber konstanta.
//   - Phi: seluruh operand bernilai konstan SAMA -> phi ikut konstan
//     (fixpoint lokal untuk rantai phi lintas block).
//   - Float memakai JALUR KHUSUS: operand ValueV ditulis ulang menjadi
//     ConstantV — referensi constant pool module (IrConstant::FloatBits,
//     64-bit). ImmV hanya 32-bit sehingga tidak bisa membawa bit pattern
//     desimal; ConstantV tidak dibatasi demikian. Pool di-dedup
//     (tipe + bit pattern) lewat IrEditor::appendConst.
//
// Batasan: bukan copy propagation (hanya konstanta), intraprosedural.
class ConstantPropagation final : public IrPass {
public:
    std::string_view name() const override { return "const-prop"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
