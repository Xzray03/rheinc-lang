#include "RhOptimizer/StoreForwarding.h"

#include "RhIR/IrDomAnalysis.h"
#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <cstdio>

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

IrPassResult StoreForwarding::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.instrs.empty()) continue;
        const DomInfo dom = computeDom(fn);

        // Kumpulkan store/load per alamat: key = alamat.
        // SSA pointer: key = 0x80000000 | ValueId. GlobalV: key = id.
        auto keyOf = [&fn](const IrInstr& ins, bool* ok) -> std::uint32_t {
            *ok = false;
            if (ins.operands.empty()) return 0;
            const IrOperand& a = ins.operands[0];
            if (a.kind == IrOperandKind::ValueV) {
                *ok = true;
                return 0x80000000u | a.id;
            }
            if (a.kind == IrOperandKind::GlobalV) {
                *ok = true;
                return a.id;
            }
            return 0;
        };
        std::unordered_map<std::uint32_t, std::vector<InstrId>> stores;
        std::unordered_map<std::uint32_t, std::vector<InstrId>> loads;
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            for (const InstrId iid : fn.blocks[b].insts) {
                if (iid >= fn.instrs.size()) continue;
                const IrInstr& ins = fn.instrs[iid];
                bool ok = true;
                const std::uint32_t key = keyOf(ins, &ok);
                if (!ok) continue;
                if (ins.op == IrOpcode::Store) stores[key].push_back(iid);
                else if (ins.op == IrOpcode::Load) loads[key].push_back(iid);
            }
        }
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

        for (auto& kv : loads) {
            const std::uint32_t key = kv.first;
            const std::vector<InstrId>& slist = stores[key];
            if (slist.empty()) continue;
            for (const InstrId lid : kv.second) {
                const BlockId lb = blockOf(lid);
                if (lb == InvalidBlock) continue;
                const IrInstr loadI = fn.instrs[lid];  // snapshot
                // cari store kandidat.
                std::int32_t cand = -1;
                bool ambiguous = false;
                for (std::size_t si = 0; si < slist.size(); ++si) {
                    const BlockId sb = blockOf(slist[si]);
                    if (sb == InvalidBlock) continue;
                    // syarat 1: dom(S, B)
                    if (!dom.dominates(lb, sb)) continue;
                    bool s2 = true;
                    if (slist.size() > 1) {
                        for (std::size_t sj = 0; sj < slist.size(); ++sj) {
                            if (sj == si) continue;
                            const BlockId sb2 = blockOf(slist[sj]);
                            if (sb2 == InvalidBlock ||
                                !dom.dominates(sb, sb2)) {
                                s2 = false;
                                break;
                            }
                        }
                    }
                    if (!s2) continue;
                    // syarat 3 (hanya multi-store): tiap pred P didominasi
                    // S; store ke alamat ini dalam P harus S SENDIRI
                    // (P == S.blk sah -- store kandidat adalah yang terakhir
                    // sebelum edge; P berisi store lain -> store itu yang
                    // terakhir -> S bukan penampung).
                    if (slist.size() > 1) {
                        bool s3 = true;
                        for (const BlockId p : fn.blocks[lb].preds) {
                            if (!dom.dominates(p, sb)) {
                                s3 = false;
                                break;
                            }
                            for (const InstrId pid : fn.blocks[p].insts) {
                                bool pok = true;
                                if (fn.instrs[pid].op == IrOpcode::Store &&
                                    keyOf(fn.instrs[pid], &pok) == key &&
                                    pid != slist[si]) {
                                    s3 = false;
                                    break;
                                }
                            }
                            if (!s3) break;
                        }
                        if (!s3) continue;
                    }
                    // syarat 4: bila lb == sb, Load sesudah Store.
                    if (lb == sb &&
                        posIn(lb, lid) <= posIn(sb, slist[si])) {
                        continue;
                    }
                    if (cand != -1) {
                        ambiguous = true;
                        break;
                    }
                    cand = static_cast<std::int32_t>(si);
                }
                if (ambiguous || cand < 0) continue;
                const IrInstr storeI = fn.instrs[slist[cand]];  // snapshot
                if (storeI.operands.size() < 2 ||
                    storeI.operands[1].kind != IrOperandKind::ValueV) {
                    continue;
                }
                const ValueId v = storeI.operands[1].id;
                const ValueId loadV = valueOfInstr(fn, lid);
                if (loadV == InvalidValue) continue;
                std::uint32_t nRewritten = 0;
                // Alihkan semua use SSA hasil Load -> v.
                for (BlockId wb = 0; wb < fn.blocks.size(); ++wb) {
                    IrBlock& wblk = fn.blocks[wb];
                    for (std::size_t wp = 0; wp < wblk.insts.size(); ++wp) {
                        const InstrId wid = wblk.insts[wp];
                        if (wid >= fn.instrs.size()) continue;
                        const std::vector<IrOperand> wops =
                            fn.instrs[wid].operands;  // snapshot
                        for (std::size_t woi = 0; woi < wops.size(); ++woi) {
                            if (wops[woi].kind == IrOperandKind::ValueV &&
                                wops[woi].id == loadV) {
                                if (!editor.replaceOperandAt(
                                        f, wb, wp, woi,
                                        IrOperand::value(v)).ok) {
                                    result.ok = false;
                                    return result;
                                }
                                nRewritten += 1;
                            }
                        }
                    }
                }
                // changed hanya bila benar-benar ada USE terganti (Load
                // zero-use di putaran berikutnya bukan perubahan).
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
