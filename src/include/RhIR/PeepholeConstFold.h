#pragma once

#include "RhIR/OptimizerManager.h"

namespace rhc {

// PeepholeConstFold: fold binop aritmetika integer pada dua operand
// ImmV menjadi satu ConstInt. Contoh: Add(imm 40, imm 2) -> ConstInt 42.
//
// Cakupan fold (index-preserving, via IrEditor):
//   - Aritmetika/bitwise: Add/Sub/Mul/Div/Mod/Pow/DivFloor/BitAnd/BitOr/
//     BitXor/Shl/Shr — aware signedness dari resultType; Div/Mod dengan
//     pembagi nol DILEWATI (biarkan runtime memutuskan); Shl/Shr >= 32
//     dilewati (UB). Pow dibatasi eksponen <= 10000.
//   - Unary: Neg/BitNot (ConstInt), Not (ConstBool).
//   - Perbandingan CmpEq..CmpGe -> ConstBool.
//   - Instruksi hasil fold menimpa instruksi binop di InstrId yang sama
//     (resultType tak berubah); dua ConstInt operand TIDAK di-DCE
//     (penghapusan butuh operasi renumber -> fase 2).
//   - Operand dapat berupa ImmV langsung ATAU ValueV yang didefinisikan
//     oleh instruksi ConstInt(ImmV) di value table fungsi yang sama
//     (resolusi use-def lokal, tanpa analisis antar-block).
class PeepholeConstFold final : public IrPass {
public:
    std::string_view name() const override { return "peephole-const-fold"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
