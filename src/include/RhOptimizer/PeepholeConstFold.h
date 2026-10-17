#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// PeepholeConstFold: fold binop aritmetika integer pada dua operand
// ImmV menjadi satu ConstInt. Contoh: Add(imm 40, imm 2) -> ConstInt 42.
//
// CONSTANT FOLDING PENUH (index-preserving, via IrEditor; BUKAN
// constant propagation — operand non-konstan tidak diganti, hanya
// instruksi yang SELURUH operandnya konstan / aturan satu sisi yang
// terbukti yang di-fold).
//   - Integer: Add/Sub/Mul/Div/Mod/Pow/DivFloor/BitAnd/BitOr/BitXor/
//     Shl/Shr — aware signedness; Div/Mod pembagi nol DILEWATI;
//     Shl/Shr >= 32 dilewati; Pow eksponen <= 10000.
//   - Float (opcode sama, typed via tipe): Add/Sub/Mul/Div (IEEE,
//     inf/NaN aman), Mod=fmod, Pow=pow, DivFloor=floor(a/b), CmpF*
//     -> ConstBool; f32 dihitung dalam presisi float host.
//   - Unary: Neg/BitNot int, Neg float, Not bool.
//   - Perbandingan CmpEq..CmpGe: int bertanda/tak bertanda + float.
//   - Logika And/Or: aturan satu sisi (And(false,x)=false,
//     Or(true,x)=true) + fold penuh bila dua operand diketahui.
//   - Cast konstan: i2i (trunc/extend), i2f, f2i (truncate-to-zero),
//     f2f (round), <-> bool — menghasilkan konstanta tipe target.
//   - Resolve: ImmV langsung atau ValueV dari ConstInt/ConstFloat/
//     ConstBool (f32 = 1 ImmV, f64 = 2 ImmV).
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
