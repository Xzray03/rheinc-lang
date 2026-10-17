#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// EscapeAnalysis: deteksi alokasi `Alloc` yang TIDAK ESCAPE dari fungsi,
// lalu dua transformasi sound di atasnya:
//
//   A) SCALAR PROMOTION (store-to-load forwarding): bila tepat SATU
//      instruksi Store menulis ke ptr dan block-nya mendominasi semua
//      Load dari ptr -> tiap Load diganti value yang disimpan.
//   B) DEAD LOCAL ALLOCATION: bila tak ada Load dari ptr sama sekali ->
//      Alloc + seluruh Store ke ptr dieliminasi (via
//      IrEditor::eliminateInstrs -- instruksi non-murni di luar
//      whitelist DCE).
//
// Analisis escape (konservatif -- ptr dinyatakan ESCAPE bila):
//   - ptr (atau value turunan: Cast/AddrOf/Phi yang memuatnya) dipakai
//     sebagai argumen Call/CallIndirect, atau sebagai VALUE Store
//     (disimpan ke alamat/global lain), atau operand Ret, atau operand
//     Phi, atau operand Load/Store/GlobalV selain pola lokal di bawah;
//   - ptr dipakai oleh opcode apa pun yang tidak dikenali sebagai
//     akses-lokal -> escape (default aman).
//   Akses LOKAL (tidak escape): Store[ptr, v] (ptr sebagai alamat) dan
//   Load[ptr] (baca). Extract/Insert pada ptr dianggap lokal (akses
//   field, setara Load/Store alamat).
//
// Pola multi-store dengan interleaving kompleks (butuh dataflow slot)
// DILEWATI -- dokumentasi scope lengkap yang didukung secara sound.
class EscapeAnalysis final : public IrPass {
public:
    std::string_view name() const override { return "escape-analysis"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
