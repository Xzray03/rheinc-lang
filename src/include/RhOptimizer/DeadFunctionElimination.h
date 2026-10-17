#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// DeadFunctionElimination: menghapus fungsi yang tidak pernah menjadi
// target operand FuncV (Call [0] / AddrOf [0]) di manapun -- tidak
// terpanggil dan tidak di-referensikan sebagai nilai (lambda). Entry dan
// GlobalInit dijaga. Fungsi rekursif tetap hidup via referensi diri.
// Kompaksi + remap FuncId via IrEditor::removeDeadFunctions.
class DeadFunctionElimination final : public IrPass {
public:
    std::string_view name() const override { return "dead-func-elim"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
