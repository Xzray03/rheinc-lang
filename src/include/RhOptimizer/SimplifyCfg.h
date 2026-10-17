#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// SimplifyCfg: merge block linier -- pred dengan succ tunggal B
// (pred.succs == {B}) dan B dengan pred tunggal (B.preds == {pred}),
// B bukan entry -> digabung jadi satu block via IrEditor::mergeBlocks
// (instruksi dipindahkan, operand BlockV di-remap, orphan disapu
// removeUnreachableBlocks). Iterasi sampai tidak ada merge lagi.
// Mengurangi block/jump hasil lowering (each.then/else, join, dst) dan
// mempercepat pass dominasi-based.
class SimplifyCfg final : public IrPass {
public:
    std::string_view name() const override { return "simplify-cfg"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
