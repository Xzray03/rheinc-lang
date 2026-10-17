#pragma once

#include "RhOptimizer/OptimizerManager.h"

namespace rhc {

// DeadCodeElim: menghapus instruksi murni yang SSA value-nya tidak
// terpakai, via IrEditor::removeDeadInstrs (function-lokal, index-safe).
// Whitelist op murni konservatif (ConstInt/Add/Sub/Mul); op dengan efek
// samping tidak pernah dihapus. Lazim dijalankan SETELAH pass yang
// menghasilkan instruksi mati (mis. PeepholeConstFold).
class DeadCodeElim final : public IrPass {
public:
    std::string_view name() const override { return "dead-code-elim"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
