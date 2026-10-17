#include "RhOptimizer/DeadStoreElimination.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rhc {

IrPassResult DeadStoreElimination::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.instrs.empty()) continue;

        // ---- escape check per SSA pointer (use-graph BFS) ----
        // pointer candidat: SSA value yang jadi alamat Store/Load.
        auto isLocalUse = [](const IrInstr& u, std::size_t oi) {
            return (u.op == IrOpcode::Store && oi == 0) ||
                   (u.op == IrOpcode::Load && oi == 0);
        };
        auto escapes = [&](ValueId p) {
            std::vector<std::uint8_t> esc(fn.values.size(), 0);
            std::vector<ValueId> wl{p};
            while (!wl.empty()) {
                const ValueId v = wl.back();
                wl.pop_back();
                if (esc[v]) continue;
                esc[v] = 1;
                for (const IrBlock& blk : fn.blocks) {
                    for (const InstrId iid : blk.insts) {
                        if (iid >= fn.instrs.size()) continue;
                        const IrInstr& u = fn.instrs[iid];
                        for (std::size_t oi = 0; oi < u.operands.size();
                             ++oi) {
                            const IrOperand& o = u.operands[oi];
                            if (o.kind != IrOperandKind::ValueV ||
                                o.id != v) {
                                continue;
                            }
                            if (isLocalUse(u, oi)) continue;
                            if (u.op == IrOpcode::Cast ||
                                u.op == IrOpcode::AddrOf) {
                                for (std::uint32_t vv = 0;
                                     vv < fn.values.size(); ++vv) {
                                    if (fn.values[vv].kind ==
                                            IrValue::Kind::Instr &&
                                        fn.values[vv].payload == iid) {
                                        wl.push_back(vv);
                                    }
                                }
                                continue;
                            }
                            return true;  // escape
                        }
                    }
                }
            }
            return false;
        };

        // ---- scan per block: tandai store yang ditimpa ----
        std::vector<bool> dead(fn.instrs.size(), false);
        bool any = false;
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& blk = fn.blocks[b];
            // per pointer: posisi store terakhir yang masih mungkin terbaca.
            std::unordered_map<std::uint32_t, std::size_t> lastStorePos;
            std::unordered_map<std::uint32_t, InstrId> lastStoreId;
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid >= fn.instrs.size()) continue;
                const IrInstr& ins = fn.instrs[iid];
                if (ins.op == IrOpcode::Call ||
                    ins.op == IrOpcode::CallIndirect) {
                    // barrier konservatif: semua store pending jadi
                    // mungkin terbaca lewat pemanggilan.
                    lastStorePos.clear();
                    lastStoreId.clear();
                    continue;
                }
                if (ins.operands.empty()) continue;
                const IrOperand& a = ins.operands[0];
                if (a.kind != IrOperandKind::ValueV) continue;
                const std::uint32_t key = a.id;
                if (ins.op == IrOpcode::Load) {
                    lastStorePos.erase(key);
                    lastStoreId.erase(key);
                    continue;
                }
                if (ins.op == IrOpcode::Store) {
                    if (escapes(key)) continue;  // pointer escape -> skip
                    const auto it = lastStorePos.find(key);
                    if (it != lastStorePos.end()) {
                        // store sebelumnya ditimpa -> dead.
                        dead[lastStoreId[key]] = true;
                        any = true;
                    }
                    lastStorePos[key] = pos;
                    lastStoreId[key] = iid;
                }
            }
        }
        if (any) {
            if (!editor.eliminateInstrs(f, dead).ok) {
                result.ok = false;
                return result;
            }
            result.changed = true;
            std::uint32_t n = 0;
            for (const bool d : dead) n += d ? 1u : 0u;
            result.folded += n;
        }
    }
    return result;
}

} // namespace rhc
