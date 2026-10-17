#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// StrengthReduction: mengganti operasi mahal dengan operasi murah
// (index-preserving via IrEditor; transformasi 1->N memakai
// insertInstrBefore). Hanya integer; bila kedua operand konstan,
// dibiarkan untuk PeepholeConstFold.
//
// Aturan (c = konstanta, x = nilai tak-konstan, k = log2 c bila c
// pangkat dua):
//   Mul : x*0 -> And(x,0) ; x*1 -> Shl(x,0) ; x*2^k -> Shl(x,k) ;
//         x*c umum -> dekomposisi shift-add (2^a+2^b+...) via
//         insertInstrBefore (popcount-1 sisipan), konstan dinormalisasi
//         ke kanan bila perlu (Mul komutatif).
//   Add : x+x -> Shl(x,1) ; konstan dinormalisasi ke kanan.
//   Div/DivFloor : x/2^k -> Shr (signed: HANYA DivFloor = arithmetic
//         shift karena floor-div; signed Div (trunc) DILEWATI; unsigned
//         keduanya -> logical shift).
//   Mod : x%2^k -> And(x, c-1) untuk UNSIGNED saja (floor-mod signed
//         tak ekuivalen BitAnd untuk a<0).
//   Pow : eksponen konstan 2..16 -> rantai perkalian biner (x^2 ->
//         Mul(x,x) in-place; x^(2m) -> (x^m)^2; umumnya via sisipan),
//         basis konstan saja (eksponen tak-konstan = runtime).
class StrengthReduction final : public IrPass {
public:
    std::string_view name() const override { return "strength-reduce"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
