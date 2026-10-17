#include "RhOptimizer/DeadFunctionElimination.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <vector>

namespace rhc {

IrPassResult DeadFunctionElimination::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    std::vector<std::uint8_t> used(module.functions.size(), 0);
    for (const IrFunction& fn : module.functions) {
        for (const IrBlock& blk : fn.blocks) {
            for (const InstrId iid : blk.insts) {
                if (iid >= fn.instrs.size()) continue;
                for (const IrOperand& op : fn.instrs[iid].operands) {
                    if (op.kind == IrOperandKind::FuncV &&
                        op.id < used.size()) {
                        used[op.id] = 1;
                    }
                }
            }
        }
    }
    // Symbol table = antarmuka eksternal modul: fungsi terdaftar di
    // symbol mapping dianggap terpakai meski tak ada operand FuncV
    // (invariant verifier: symbol global harus merujuk fungsi valid).
    module.symbolMapping.markMappedFunctions(used);
    std::vector<bool> dead(module.functions.size(), false);
    bool any = false;
    for (std::uint32_t i = 0; i < module.functions.size(); ++i) {
        const IrFunctionKind k = module.functions[i].kind;
        // Entry & GlobalInit dijaga (invariant modul); External (stub
        // runtime/method object) juga: stub tidak selalu jadi operand
        // FuncV / symbol mapping, tapi wajib ada di tabel fungsi
        // (verifier external memeriksanya -- method object seperti
        // 'potong', 'daftar' dsb dari registry).
        if (k == IrFunctionKind::Entry || k == IrFunctionKind::GlobalInit ||
            k == IrFunctionKind::External) {
            continue;
        }
        if (!used[i]) {
            dead[i] = true;
            any = true;
        }
    }
    if (any) {
        if (!editor.removeDeadFunctions(dead).ok) {
            result.ok = false;
            return result;
        }
        result.changed = true;
        std::uint32_t n = 0;
        for (const bool d : dead) n += d ? 1u : 0u;
        result.folded += n;
    }
    return result;
}

} // namespace rhc
