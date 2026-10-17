#include "RhOptimizer/Devirtualization.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <functional>
#include <vector>

namespace rhc {

namespace {

// Resolve SSA value fnptr -> FuncId target statis (AddrOf), atau
// InvalidFunc bila dinamis. Phi: semua incoming harus resolve ke fungsi
// yang sama. visited mencegah siklus.
std::uint32_t resolveStaticTarget(const IrFunction& fn, ValueId v,
                                  std::vector<std::uint8_t>& visited) {
    if (v >= fn.values.size() || visited[v]) return InvalidFunc;
    visited[v] = 1;
    const IrValue& val = fn.values[v];
    if (val.kind != IrValue::Kind::Instr || val.payload >= fn.instrs.size()) {
        return InvalidFunc;
    }
    const IrInstr& def = fn.instrs[val.payload];
    if (def.op == IrOpcode::AddrOf && def.operands.size() == 1 &&
        def.operands[0].kind == IrOperandKind::FuncV) {
        return def.operands[0].id;
    }
    if (def.op == IrOpcode::Phi) {
        std::uint32_t target = InvalidFunc;
        for (std::size_t i = 1; i < def.operands.size(); i += 2) {
            const IrOperand& inc = def.operands[i];
            if (inc.kind != IrOperandKind::ValueV) return InvalidFunc;
            const std::uint32_t t = resolveStaticTarget(fn, inc.id, visited);
            if (t == InvalidFunc) return InvalidFunc;
            if (target == InvalidFunc) {
                target = t;
            } else if (target != t) {
                return InvalidFunc;  // polimorfik
            }
        }
        return target;
    }
    return InvalidFunc;
}

} // namespace

IrPassResult Devirtualization::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& blk = fn.blocks[b];
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid == InvalidInstr || iid >= fn.instrs.size()) continue;
                const IrInstr instr = fn.instrs[iid];  // snapshot
                if (instr.op != IrOpcode::CallIndirect ||
                    instr.operands.empty() ||
                    instr.operands[0].kind != IrOperandKind::ValueV) {
                    continue;
                }
                std::vector<std::uint8_t> visited(fn.values.size(), 0);
                const std::uint32_t target =
                    resolveStaticTarget(fn, instr.operands[0].id, visited);
                if (target == InvalidFunc || target >= module.functions.size()) {
                    continue;
                }
                IrInstr direct = instr;
                direct.op = IrOpcode::Call;
                direct.operands[0] = IrOperand::func(target);
                direct.name.clear();
                if (!editor.replaceInstr(f, b, pos, direct).ok) {
                    result.ok = false;
                    return result;
                }
                result.changed = true;
                result.folded += 1;
            }
        }
    }
    return result;
}

} // namespace rhc
