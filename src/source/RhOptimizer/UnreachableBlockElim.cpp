#include "RhOptimizer/UnreachableBlockElim.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrModule.h"

namespace rhc {

IrPassResult UnreachableBlockElim::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);
    for (FuncId f = 0; f < module.functions.size(); ++f) {
        std::uint32_t removed = 0;
        std::uint32_t foldedCondBr = 0;
        const IrEditorReport step =
            editor.removeUnreachableBlocks(f, removed, foldedCondBr);
        if (!step.ok) {
            result.ok = false;
            return result;
        }
        // CondBr-fold adalah mutasi IR nyata (walau tanpa block terhapus)
        // — wajib dilaporkan agar konsisten dengan change-detection net
        // OptimizerManager (epochDelta > 0 <-> changed == true).
        if (removed > 0 || foldedCondBr > 0) {
            result.changed = true;
            result.folded += removed + foldedCondBr;
        }
    }
    return result;
}

} // namespace rhc
