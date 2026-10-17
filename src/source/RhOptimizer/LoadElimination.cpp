#include "RhOptimizer/LoadElimination.h"

#include "RhIR/IrDomAnalysis.h"
#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rhc {

namespace {

ValueId valueOfInstr(const IrFunction& fn, InstrId iid) {
    for (std::uint32_t v = 0; v < fn.values.size(); ++v) {
        if (fn.values[v].kind == IrValue::Kind::Instr &&
            fn.values[v].payload == iid) {
            return v;
        }
    }
    return InvalidValue;
}

} // namespace

IrPassResult LoadElimination::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.instrs.empty()) continue;
        const DomInfo dom = computeDom(fn);
        const std::uint32_t nB = fn.blocks.size();

        // reachability via succ (bitset BFS).
        auto reachable = [&fn, nB](BlockId from, BlockId to) -> bool {
            if (from == to) return true;
            std::vector<std::uint8_t> vis(nB, 0);
            std::vector<BlockId> stack{from};
            vis[from] = 1;
            while (!stack.empty()) {
                const BlockId c = stack.back();
                stack.pop_back();
                for (const BlockId s : fn.blocks[c].succs) {
                    if (s >= nB) continue;
                    if (s == to) return true;
                    if (!vis[s]) {
                        vis[s] = 1;
                        stack.push_back(s);
                    }
                }
            }
            return false;
        };

        // escape check (sama seperti dead-store-elim).
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
                            if ((u.op == IrOpcode::Store && oi == 0) ||
                                (u.op == IrOpcode::Load && oi == 0)) {
                                continue;
                            }
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
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        auto blockOf = [&fn](InstrId iid) -> BlockId {
            for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                for (const InstrId id : fn.blocks[b].insts) {
                    if (id == iid) return b;
                }
            }
            return InvalidBlock;
        };
        auto posIn = [&fn](BlockId bb, InstrId id) -> std::size_t {
            const IrBlock& B = fn.blocks[bb];
            for (std::size_t i = 0; i < B.insts.size(); ++i) {
                if (B.insts[i] == id) return i;
            }
            return B.insts.size();
        };

        // kumpulkan load per SSA pointer (hanya ValueV alamat).
        std::unordered_map<std::uint32_t, std::vector<InstrId>> loads;
        std::unordered_map<std::uint32_t, std::vector<InstrId>> stores;
        for (BlockId b = 0; b < nB; ++b) {
            for (const InstrId iid : fn.blocks[b].insts) {
                if (iid >= fn.instrs.size()) continue;
                const IrInstr& ins = fn.instrs[iid];
                if (ins.operands.empty() ||
                    ins.operands[0].kind != IrOperandKind::ValueV) {
                    continue;
                }
                const std::uint32_t key = ins.operands[0].id;
                if (ins.op == IrOpcode::Load) loads[key].push_back(iid);
                else if (ins.op == IrOpcode::Store) {
                    stores[key].push_back(iid);
                }
            }
        }

        for (auto& kv : loads) {
            const std::uint32_t key = kv.first;
            if (escapes(key)) continue;
            const std::vector<InstrId>& llist = kv.second;
            const std::vector<InstrId>& slist = stores[key];
            for (std::size_t j = 1; j < llist.size(); ++j) {
                const InstrId l2 = llist[j];
                const BlockId b2 = blockOf(l2);
                if (b2 == InvalidBlock) continue;
                // cari L1 dominan: load lain yang mendominasi b2.
                std::int32_t cand = -1;
                for (std::size_t i = 0; i < j; ++i) {
                    const InstrId l1 = llist[i];
                    const BlockId b1 = blockOf(l1);
                    if (b1 == InvalidBlock) continue;
                    if (!dom.dominates(b2, b1)) continue;  // syarat 1
                    // syarat 2: tidak ada store S[p] yang dieksekusi
                    // setelah L1 dan sebelum L2.
                    bool invalid = false;
                    for (const InstrId sid : slist) {
                        const BlockId sb = blockOf(sid);
                        if (sb == InvalidBlock) continue;
                        if (sb == b1 &&
                            posIn(sb, sid) < posIn(b1, l1)) {
                            continue;  // S sebelum L1 -> tercermin di L1
                        }
                        if (reachable(sb, b2) && reachable(b1, sb)) {
                            invalid = true;
                            break;
                        }
                    }
                    if (invalid) continue;
                    cand = static_cast<std::int32_t>(i);
                    break;
                }
                if (cand < 0) continue;
                const ValueId v1 = valueOfInstr(fn, llist[cand]);
                const ValueId v2 = valueOfInstr(fn, l2);
                if (v1 == InvalidValue || v2 == InvalidValue) continue;
                std::uint32_t nRewritten = 0;
                for (BlockId wb = 0; wb < nB; ++wb) {
                    IrBlock& wblk = fn.blocks[wb];
                    for (std::size_t wp = 0; wp < wblk.insts.size(); ++wp) {
                        const InstrId wid = wblk.insts[wp];
                        if (wid >= fn.instrs.size()) continue;
                        const std::vector<IrOperand> wops =
                            fn.instrs[wid].operands;  // snapshot
                        for (std::size_t woi = 0; woi < wops.size(); ++woi) {
                            if (wops[woi].kind == IrOperandKind::ValueV &&
                                wops[woi].id == v2) {
                                if (!editor.replaceOperandAt(
                                        f, wb, wp, woi,
                                        IrOperand::value(v1)).ok) {
                                    result.ok = false;
                                    return result;
                                }
                                nRewritten += 1;
                            }
                        }
                    }
                }
                if (nRewritten > 0) {
                    result.folded += nRewritten;
                    result.changed = true;
                }
            }
        }
    }
    return result;
}

} // namespace rhc
