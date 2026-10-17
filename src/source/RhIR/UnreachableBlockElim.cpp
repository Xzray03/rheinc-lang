#include "RhIR/UnreachableBlockElim.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrModule.h"

namespace rhc {

IrPassResult UnreachableBlockElim::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);
    for (FuncId f = 0; f < module.functions.size(); ++f) {
        std::uint32_t removed = 0;
        const IrEditorReport step = editor.removeUnreachableBlocks(f, removed);
        if (!step.ok) {
            result.ok = false;
            return result;
        }
        if (removed > 0) {
            result.changed = true;
            result.folded += removed;  // statistik generik: jumlah block
        }
    }
    return result;
}

} // namespace rhc
