#include "RhOptimizer/ScalarReplacement.h"

#include "RhIR/IrDomAnalysis.h"
#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
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

bool isLocalUse(const IrInstr& user, std::size_t oi) {
    if (user.op == IrOpcode::Store && oi == 0) return true;
    if (user.op == IrOpcode::Load && oi == 0) return true;
    if ((user.op == IrOpcode::Extract || user.op == IrOpcode::Insert) &&
        oi == 0) {
        return true;
    }
    return false;
}

} // namespace

IrPassResult ScalarReplacement::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.instrs.empty()) continue;
        const DomInfo dom = computeDom(fn);
        // block tempat tiap instruksi ter-urut (untuk remap setelah
        // eliminasi); dibangun ulang tiap alloc.
        auto blockOf = [&fn](InstrId iid) -> BlockId {
            for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                for (const InstrId id : fn.blocks[b].insts) {
                    if (id == iid) return b;
                }
            }
            return InvalidBlock;
        };

        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& allocBlk = fn.blocks[b];
            for (std::size_t pos = 0; pos < allocBlk.insts.size(); ++pos) {
                const InstrId allocId = allocBlk.insts[pos];
                if (allocId >= fn.instrs.size()) continue;
                const IrInstr allocI = fn.instrs[allocId];  // snapshot
                if (allocI.op != IrOpcode::Alloc ||
                    allocI.operands.empty() ||
                    allocI.operands[0].kind != IrOperandKind::TypeV ||
                    allocI.resultType == InvalidType) {
                    continue;
                }
                const ValueId ptr = valueOfInstr(fn, allocId);
                if (ptr == InvalidValue) continue;

                // ---- escape analysis (sama dengan escape-analysis) ----
                std::vector<std::uint8_t> esc(fn.values.size(), 0);
                std::vector<ValueId> worklist{ptr};
                bool escaped = false;
                std::vector<InstrId> stores;
                std::vector<InstrId> loads;
                while (!worklist.empty() && !escaped) {
                    const ValueId v = worklist.back();
                    worklist.pop_back();
                    if (esc[v]) continue;
                    esc[v] = 1;
                    for (BlockId ub = 0; ub < fn.blocks.size() && !escaped;
                         ++ub) {
                        const IrBlock& ublk = fn.blocks[ub];
                        for (const InstrId uiid : ublk.insts) {
                            if (uiid >= fn.instrs.size()) continue;
                            const IrInstr& u = fn.instrs[uiid];
                            for (std::size_t oi = 0; oi < u.operands.size();
                                 ++oi) {
                                const IrOperand& o = u.operands[oi];
                                if (o.kind != IrOperandKind::ValueV ||
                                    o.id != v) {
                                    continue;
                                }
                                if (isLocalUse(u, oi)) {
                                    if (u.op == IrOpcode::Store) {
                                        stores.push_back(uiid);
                                    } else if (u.op == IrOpcode::Load) {
                                        loads.push_back(uiid);
                                    }
                                    continue;
                                }
                                if (u.op == IrOpcode::Cast ||
                                    u.op == IrOpcode::AddrOf) {
                                    const ValueId dv = valueOfInstr(fn, uiid);
                                    if (u.resultType == InvalidType ||
                                        dv == InvalidValue) {
                                        escaped = true;
                                        break;
                                    }
                                    worklist.push_back(dv);
                                    continue;
                                }
                                escaped = true;
                                break;
                            }
                            if (escaped) break;
                        }
                    }
                }
                if (escaped || stores.size() < 2) {
                    // <2 store: bukan domain scalar replacement multi-store
                    // (single-store sudah ditangani escape analysis).
                    continue;
                }

                // block tiap store; cek juga tidak ada store duplikat id
                std::vector<BlockId> storeBlk(stores.size());
                bool bad = false;
                for (std::size_t i = 0; i < stores.size(); ++i) {
                    storeBlk[i] = blockOf(stores[i]);
                    if (storeBlk[i] == InvalidBlock) {
                        bad = true;
                        break;
                    }
                }
                if (bad) continue;

                // ---- aturan ketat untuk TIAP load ----
                // Struktur: untuk tiap load, cari kandidat S unik. Bila semua
                // load punya kandidat -> promosikan semua + eliminasi.
                struct LoadFix {
                    InstrId loadId;
                    ValueId v;
                };
                std::vector<LoadFix> fixes;
                bool allPromotable = !loads.empty();
                for (const InstrId lid : loads) {
                    const BlockId lb = blockOf(lid);
                    if (lb == InvalidBlock) {
                        allPromotable = false;
                        break;
                    }
                    const IrInstr loadI = fn.instrs[lid];  // snapshot
                    // posisi load dalam block (untuk syarat 4)
                    std::size_t loadPos = 0;
                    {
                        const IrBlock& L = fn.blocks[lb];
                        while (loadPos < L.insts.size() &&
                               L.insts[loadPos] != lid) {
                            ++loadPos;
                        }
                    }
                    // posisi tiap store dalam block-nya (untuk syarat 4)
                    auto posIn = [&fn](BlockId bb, InstrId id) -> std::size_t {
                        const IrBlock& B = fn.blocks[bb];
                        for (std::size_t i = 0; i < B.insts.size(); ++i) {
                            if (B.insts[i] == id) return i;
                        }
                        return B.insts.size();
                    };
                    // kandidat: store S dengan dom(S,lb), S dominasi semua
                    // store lain, semua pred lb didominasi S & bebas-store.
                    std::int32_t cand = -1;
                    for (std::size_t si = 0; si < stores.size(); ++si) {
                        const BlockId sb = storeBlk[si];
                        if (!dom.dominates(lb, sb)) continue;  // syarat 1
                        bool s2 = true;
                        for (std::size_t sj = 0; sj < stores.size(); ++sj) {
                            if (sj == si) continue;
                            // S.blk harus DIDAHULUI semua store lain:
                            // storeBlk[sj] MENDOMINASI sb (semua jalur ke
                            // sb melewati store lain -> S dieksekusi
                            // sesudah mereka).
                            if (!dom.dominates(sb, storeBlk[sj])) {
                                s2 = false;
                                break;
                            }
                        }
                        if (!s2) continue;  // syarat 2
                        bool s3 = true;
                        for (const BlockId p : fn.blocks[lb].preds) {
                            if (!dom.dominates(p, sb)) {
                                s3 = false;
                                break;
                            }
                            // P bebas store ke ptr?
                            for (const InstrId pid : fn.blocks[p].insts) {
                                const IrInstr& pi = fn.instrs[pid];
                                if (pi.op == IrOpcode::Store &&
                                    !pi.operands.empty() &&
                                    pi.operands[0].kind ==
                                        IrOperandKind::ValueV &&
                                    pi.operands[0].id == ptr) {
                                    s3 = false;
                                    break;
                                }
                            }
                            if (!s3) break;
                        }
                        if (!s3) continue;  // syarat 3
                        // syarat 4: bila lb == sb, load harus sesudah store
                        if (lb == sb &&
                            posIn(lb, lid) <= posIn(sb, stores[si])) {
                            continue;
                        }
                        if (cand != -1) {
                            // dua kandidat -> ambiguitas, skip alloc ini
                            cand = -2;
                            break;
                        }
                        cand = static_cast<std::int32_t>(si);
                    }
                    if (cand < 0) {
                        allPromotable = false;
                        break;
                    }
                    const IrInstr storeI = fn.instrs[stores[cand]];  // snap
                    if (storeI.operands.size() < 2 ||
                        storeI.operands[1].kind != IrOperandKind::ValueV) {
                        allPromotable = false;
                        break;
                    }
                    fixes.push_back({lid, storeI.operands[1].id});
                }
                if (!allPromotable) continue;

                // ---- promosikan: alihkan use SSA hasil Load -> v ----
                for (const LoadFix& fx : fixes) {
                    const ValueId loadV = valueOfInstr(fn, fx.loadId);
                    if (loadV == InvalidValue) continue;
                    for (BlockId wb = 0; wb < fn.blocks.size(); ++wb) {
                        IrBlock& wblk = fn.blocks[wb];
                        for (std::size_t wp = 0; wp < wblk.insts.size();
                             ++wp) {
                            const InstrId wid = wblk.insts[wp];
                            if (wid >= fn.instrs.size()) continue;
                            const std::vector<IrOperand> wops =
                                fn.instrs[wid].operands;  // snapshot
                            for (std::size_t woi = 0; woi < wops.size();
                                 ++woi) {
                                if (wops[woi].kind == IrOperandKind::ValueV &&
                                    wops[woi].id == loadV) {
                                    if (!editor.replaceOperandAt(
                                            f, wb, wp, woi,
                                            IrOperand::value(fx.v)).ok) {
                                        result.ok = false;
                                        return result;
                                    }
                                }
                            }
                        }
                    }
                }
                // ---- eliminasi: alloc + semua store (tak ada Load tersisa)
                std::vector<bool> dead(fn.instrs.size(), false);
                dead[allocId] = true;
                for (const InstrId sid : stores) dead[sid] = true;
                if (!editor.eliminateInstrs(f, dead).ok) {
                    result.ok = false;
                    return result;
                }
                result.changed = true;
                result.folded += static_cast<std::uint32_t>(
                    fixes.size() + stores.size() + 1);
            }
        }
    }
    return result;
}

} // namespace rhc
