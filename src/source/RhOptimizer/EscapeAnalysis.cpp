#include "RhOptimizer/EscapeAnalysis.h"

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

// Apakah pemakaian operand o terhadap ptr (SSA value dari Alloc)
// merupakan akses-lokal? o adalah salah satu operand dari instruksi
// yang mengandung ptr (bisa sebagai alamat Store/Load, atau sebagai
// value). Escape dilaporkan lewat out-escape saat pemakaian bukan lokal.
bool isLocalUse(const IrInstr& user, std::size_t oi, ValueId ptr) {
    (void)ptr;
    if (user.op == IrOpcode::Store && oi == 0) return true;   // alamat
    if (user.op == IrOpcode::Load && oi == 0) return true;    // baca
    if ((user.op == IrOpcode::Extract || user.op == IrOpcode::Insert) &&
        oi == 0) {
        return true;  // akses field via ptr = lokal
    }
    return false;
}

} // namespace

IrPassResult EscapeAnalysis::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.instrs.empty()) continue;
        const DomInfo dom = computeDom(fn);

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

                // ---- escape analysis (BFS over SSA use graph) ----
                std::vector<std::uint8_t> esc(fn.values.size(), 0);
                std::vector<ValueId> worklist{ptr};
                bool escaped = false;
                std::vector<InstrId> stores;      // Store ke ptr (lokal)
                std::vector<InstrId> loads;       // Load dari ptr
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
                                // user == alloc itu sendiri? tak mungkin.
                                if (isLocalUse(u, oi, v)) {
                                    if (u.op == IrOpcode::Store) {
                                        stores.push_back(uiid);
                                    } else if (u.op == IrOpcode::Load) {
                                        loads.push_back(uiid);
                                    }
                                    continue;
                                }
                                // ptr sebagai value -> derived (Cast/AddrOf/
                                // Phi) mengalirkan escape; selain itu escape.
                                if (u.op == IrOpcode::Cast ||
                                    u.op == IrOpcode::AddrOf) {
                                    if (u.resultType == InvalidType) {
                                        escaped = true;
                                        break;
                                    }
                                    const ValueId dv = valueOfInstr(fn, uiid);
                                    if (dv == InvalidValue) {
                                        escaped = true;
                                        break;
                                    }
                                    worklist.push_back(dv);
                                    continue;
                                }
                                // Phi operand -> escape (konservatif:
                                // bisa mengalir keluar lewat join).
                                // Call args / Store-as-value / Ret / lainnya.
                                escaped = true;
                                break;
                            }
                            if (escaped) break;
                        }
                    }
                }
                if (escaped) continue;

                // ---- transformasi B: dead local allocation (tanpa Load)
                if (loads.empty()) {
                    std::vector<bool> dead(fn.instrs.size(), false);
                    dead[allocId] = true;
                    for (const InstrId sid : stores) dead[sid] = true;
                    if (!editor.eliminateInstrs(f, dead).ok) {
                        result.ok = false;
                        return result;
                    }
                    result.changed = true;
                    result.folded +=
                        static_cast<std::uint32_t>(stores.size() + 1);
                    continue;
                }

                // ---- transformasi A: scalar promotion (satu Store
                // dominan) ----
                if (stores.size() != 1) continue;
                const InstrId storeId = stores[0];
                // block tempat Store berada
                BlockId storeBlock = InvalidBlock;
                for (BlockId sb = 0; sb < fn.blocks.size(); ++sb) {
                    for (const InstrId id : fn.blocks[sb].insts) {
                        if (id == storeId) {
                            storeBlock = sb;
                            break;
                        }
                    }
                    if (storeBlock != InvalidBlock) break;
                }
                if (storeBlock == InvalidBlock) continue;
                // Store [ptr, v]: value yang disimpan = operands[1].
                // (Store ber-InvalidType tak punya SSA value -- tidak
                // relevan, yang dipakai adalah operands[1].)
                const IrInstr storeI = fn.instrs[storeId];  // snapshot
                if (storeI.operands.size() < 2 ||
                    storeI.operands[1].kind != IrOperandKind::ValueV) {
                    continue;
                }
                const ValueId v = storeI.operands[1].id;
                // Store-block harus mendominasi semua Load-block.
                bool dominatesAll = true;
                for (const InstrId lid : loads) {
                    BlockId loadBlock = InvalidBlock;
                    for (BlockId lb = 0; lb < fn.blocks.size(); ++lb) {
                        for (const InstrId id : fn.blocks[lb].insts) {
                            if (id == lid) {
                                loadBlock = lb;
                                break;
                            }
                        }
                        if (loadBlock != InvalidBlock) break;
                    }
                    if (loadBlock == InvalidBlock ||
                        !dom.dominates(loadBlock, storeBlock)) {
                        dominatesAll = false;
                        break;
                    }
                }
                if (!dominatesAll) continue;
                // Ganti semua Load[ptr] -> v.
                std::uint32_t promoted = 0;
                for (BlockId lb = 0; lb < fn.blocks.size(); ++lb) {
                    IrBlock& lblk = fn.blocks[lb];
                    for (std::size_t lpos = 0; lpos < lblk.insts.size();
                         ++lpos) {
                        const InstrId lid = lblk.insts[lpos];
                        if (lid >= fn.instrs.size()) continue;
                        const IrInstr li = fn.instrs[lid];  // snapshot
                        if (li.op != IrOpcode::Load || li.operands.empty() ||
                            li.operands[0].kind != IrOperandKind::ValueV ||
                            li.operands[0].id != ptr) {
                            continue;
                        }
                        // rewrite seluruh use SSA hasil Load -> v.
                        const ValueId loadV = valueOfInstr(fn, lid);
                        if (loadV == InvalidValue) continue;
                        for (BlockId wb = 0; wb < fn.blocks.size(); ++wb) {
                            IrBlock& wblk = fn.blocks[wb];
                            for (std::size_t wp = 0;
                                 wp < wblk.insts.size(); ++wp) {
                                const InstrId wid = wblk.insts[wp];
                                if (wid >= fn.instrs.size()) continue;
                                const std::vector<IrOperand> wops =
                                    fn.instrs[wid].operands;  // snapshot
                                for (std::size_t woi = 0;
                                     woi < wops.size(); ++woi) {
                                    if (wops[woi].kind ==
                                            IrOperandKind::ValueV &&
                                        wops[woi].id == loadV) {
                                        if (!editor.replaceOperandAt(
                                                f, wb, wp, woi,
                                                IrOperand::value(v)).ok) {
                                            result.ok = false;
                                            return result;
                                        }
                                    }
                                }
                            }
                        }
                        promoted += 1;
                    }
                }
                if (promoted > 0) {
                    // Alloc + Store menjadi mati -> eliminasi.
                    std::vector<bool> dead(fn.instrs.size(), false);
                    dead[allocId] = true;
                    dead[storeId] = true;
                    if (!editor.eliminateInstrs(f, dead).ok) {
                        result.ok = false;
                        return result;
                    }
                    result.changed = true;
                    result.folded += promoted + 2;
                }
            }
        }
    }
    return result;
}

} // namespace rhc
