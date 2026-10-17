#include "RhOptimizer/SimplifyCfg.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrModule.h"

#include <cstdint>

namespace rhc {

IrPassResult SimplifyCfg::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.blocks.size() < 2) continue;
        const std::uint32_t foldedBefore = result.folded;
        bool progress = true;
        while (progress) {
            progress = false;
            for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                IrBlock& blk = fn.blocks[b];
                if (blk.succs.size() != 1) continue;
                const BlockId s = blk.succs[0];
                if (s == b || s == 0 || s >= fn.blocks.size()) continue;
                if (fn.blocks[s].preds.size() != 1) continue;
                if (!editor.mergeBlocks(f, b, s).ok) {
                    result.ok = false;
                    return result;
                }
                progress = true;
                result.folded += 1;
                break;  // indeks block bisa bergeser setelah merge: ulangi
            }
        }
        // Sapu orphan hasil merge -- HANYA bila fungsi ini benar-benar
        // melakukan merge. UBE di dalamnya juga melakukan fold CondBr
        // konstan; memanggilnya tanpa merge menimbulkan mutasi siluman
        // (epoch delta > 0 tanpa changed) yang ditolak change-detection
        // net. fold CondBr adalah tugas UBE di akhir rantai, bukan sini.
        if (foldedBefore < result.folded) {
            std::uint32_t removed = 0, fcb = 0;
            if (!editor.removeUnreachableBlocks(f, removed, fcb).ok) {
                result.ok = false;
                return result;
            }
            result.changed = true;
        }
    }
    return result;
}

} // namespace rhc
