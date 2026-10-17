#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// Devirtualization: mengubah CallIndirect menjadi Call langsung bila
// target fnptr dapat DITENTUKAN STATIS.
//
// IR Rheinc tidak punya method dispatch virtual (tanpa inheritance /
// vtable -- bahasa belum mendukung); pemanggilan tidak langsung muncul
// dari first-class function: nilai lambda/fungsi = AddrOf @fn (fn ptr),
// dipanggil lewat CallIndirect. Devirtualization di sini = indirect ->
// direct call promotion untuk target yang resolve statis:
//
//   - Def operand fnptr adalah instruksi AddrOf dengan operand FuncV ->
//     target langsung (kasus monomorfik: variabel fungsi tak pernah
//     di-reassign);
//   - Def adalah Phi yang SELURUH incoming value resolve (rekursif) ke
//     AddrOf fungsi YANG SAMA -> target itu (kasus phi monomorfik);
//   - Selainnya (target dinamis / polimorfik) -> DILEWATI.
//
// Transformasi: CallIndirect [ValueV fnptr, args...] ->
//               Call [FuncV target, args...] via replaceInstr
// (resultType tak berubah; bentuk operand identik -- hanya operand[0]
// diganti). Sound: target statis memang fungsi yang dipanggil; SSA value
// hasil tetap sama; instruksi lama (AddrOf) menjadi mati bila zero-use
// -> disapu DeadCodeElim.
class Devirtualization final : public IrPass {
public:
    std::string_view name() const override { return "devirtualization"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
