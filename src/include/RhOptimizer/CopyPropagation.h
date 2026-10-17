#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// CopyPropagation: mengganti semua USE dari SSA value hasil instruksi
// IDENTITAS menjadi operand aslinya langsung. Di IR ini tidak ada opcode
// copy eksplisit; copy muncul sebagai instruksi identitas hasil transformasi
// (mis. x*1 -> Shl(x,0) oleh strength-reduce, Add(x,0), Or/Xor(x,0),
// And(x, mask penuh lebar tipe), Mul(x,1)).
//
// Sound tanpa analisis dominasi: IR memakai SSA (def tunggal). Operand v
// dari instruksi copy mendominasi block copy-nya (invariant SSA operand);
// block copy mendominasi semua use hasil copy (juga invariant SSA); maka
// v mendominasi semua use tersebut secara transitif.
//
// Bentuk identitas yang diakui (c = operand ImmV langsung atau value
// didefinisikan ConstInt/ConstBool; lebar mask dari sizeBytes resultType):
//   Add(v,0) Sub(v,0) Or(v,0) Xor(v,0) Shl(v,0) Shr(v,0) Mul(v,1)
//   And(v, mask) -- hanya tipe Int dengan mask == lebar penuh (2^(8n)-1)
// Instruksi copy mati setelah use dialihkan -> disapu DeadCodeElim.
class CopyPropagation final : public IrPass {
public:
    std::string_view name() const override { return "copy-prop"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
