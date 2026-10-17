#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// UnreachableBlockElim LENGKAP via IrEditor::removeUnreachableBlocks
// (function-lokal, index-safe), iterasi internal sampai stabil:
// fold CondBr ber-kondisi-konstan (+ perbaikan preds & incoming phi),
// penghapusan block tak terjangkau dari entry, dan orphan rescue untuk
// def instruksi yang masih dirujuk instruksi hidup.
class UnreachableBlockElim final : public IrPass {
public:
    std::string_view name() const override { return "unreachable-block-elim"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
