#pragma once

#include "RhIR/OptimizerManager.h"

namespace rhc {

// UnreachableBlockElim: menghapus block yang tak terjangkau dari block
// entry beserta instruksinya, via IrEditor::removeUnreachableBlocks
// (function-lokal, index-safe). Jalankan SETELAH pass yang mengubah
// struktur kontrol aliran.
class UnreachableBlockElim final : public IrPass {
public:
    std::string_view name() const override { return "unreachable-block-elim"; }
    IrPassResult run(IrModule& module) override;
};

} // namespace rhc
