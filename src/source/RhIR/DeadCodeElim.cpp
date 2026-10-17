#include "RhIR/DeadCodeElim.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrModule.h"

namespace rhc {

IrPassResult DeadCodeElim::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);
    for (FuncId f = 0; f < module.functions.size(); ++f) {
        std::uint32_t removed = 0;
        const IrEditorReport step = editor.removeDeadInstrs(f, removed);
        if (!step.ok) {
            result.ok = false;
            return result;
        }
        if (removed > 0) {
            result.changed = true;
            result.folded += removed;  // statistik generik: jumlah hapus
        }
    }
    return result;
}

} // namespace rhc
