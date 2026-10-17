#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// InlineFunctions: menggantikan Call ke callee kecil dengan body-nya
// yang diklon (inline expansion). Membuka seluruh pass berikutnya
// (fold/CSE/SR/store-* bekerja lintas batas fungsi).
//
// Scope v1 (ketat, sound):
//   - callee kind Normal, bukan Entry/GlobalInit/External;
//   - body SINGLE-BLOCK dengan terminator Ret;
//   - total instruksi callee <= 16;
//   - non-recursive (body tidak merujuk dirinya via operand FuncV);
//   - arg count cocok (params.size() == call.operands.size() - 1);
//   - maks 4 inline site per callee di seluruh modul.
// Mekanik: clone instruksi callee ke block call site via
// insertInstrBefore (SSA clone per instruksi), operand di-remap
// (param -> arg SSA; SSA internal lama -> clone; eksternal -> sama),
// use SSA hasil Call dialihkan ke clone nilai Ret callee, Call lama
// dieliminasi (eliminateInstrs -- Call non-murni).
class InlineFunctions final : public IrPass {
public:
    std::string_view name() const override { return "inline-functions"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
