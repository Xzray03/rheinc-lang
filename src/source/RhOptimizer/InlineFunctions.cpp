#include "RhOptimizer/InlineFunctions.h"

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

// Apakah body merujuk fungsi itu sendiri (recursive)?
bool isRecursive(const IrFunction& fn, FuncId self) {
    for (const IrBlock& blk : fn.blocks) {
        for (const InstrId iid : blk.insts) {
            if (iid >= fn.instrs.size()) continue;
            for (const IrOperand& op : fn.instrs[iid].operands) {
                if (op.kind == IrOperandKind::FuncV && op.id == self) {
                    return true;
                }
            }
        }
    }
    return false;
}

} // namespace

IrPassResult InlineFunctions::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    // kandidat callee per FunId.
    auto eligible = [&](FuncId c) -> bool {
        const IrFunction& cf = module.functions[c];
        if (cf.kind != IrFunctionKind::Normal) return false;
        if (cf.blocks.size() != 1) return false;
        if (cf.instrs.empty() || cf.blocks[0].insts.empty()) return false;
        if (cf.instrs.size() > 16) return false;
        const InstrId term = cf.blocks[0].insts.back();
        if (term >= cf.instrs.size() ||
            cf.instrs[term].op != IrOpcode::Ret) {
            return false;
        }
        if (isRecursive(cf, c)) return false;
        return true;
    };

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        std::vector<bool> dead(fn.instrs.size(), false);
        bool anyDead = false;
        std::unordered_map<FuncId, std::uint32_t> inlineCount;

        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            IrBlock& blk = fn.blocks[b];
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid >= fn.instrs.size()) continue;
                if (dead[iid]) continue;  // sudah di-inline -> lewati
                const IrInstr callI = fn.instrs[iid];  // snapshot
                if (callI.op != IrOpcode::Call ||
                    callI.operands.empty() ||
                    callI.operands[0].kind != IrOperandKind::FuncV) {
                    continue;
                }
                const FuncId c = callI.operands[0].id;
                if (c >= module.functions.size() || !eligible(c)) continue;
                if (inlineCount[c] >= 4) continue;
                const IrFunction& cf = module.functions[c];
                if (cf.params.size() + 1 != callI.operands.size()) continue;

                // ---- clone body callee ke block call site ----
                std::unordered_map<ValueId, ValueId> vmap;
                // binding param -> arg.
                for (std::size_t pi = 0; pi < cf.params.size(); ++pi) {
                    vmap[cf.params[pi]] = callI.operands[1 + pi].id;
                }
                // posisi sisipan bergeser tiap clone: selalu sebelum posisi
                // call SAAT INI (relokasi per clone).
                auto remapOp = [&](const IrOperand& op) -> IrOperand {
                    if (op.kind != IrOperandKind::ValueV) return op;
                    const auto it = vmap.find(op.id);
                    if (it != vmap.end()) return IrOperand::value(it->second);
                    return op;
                };
                // clone instruksi callee (semua kecuali terminator).
                const IrBlock& cb = cf.blocks[0];
                for (std::size_t ci = 0; ci + 1 < cb.insts.size(); ++ci) {
                    const InstrId cid = cb.insts[ci];
                    if (cid >= cf.instrs.size()) continue;
                    IrInstr clone = cf.instrs[cid];  // snapshot
                    for (IrOperand& o : clone.operands) o = remapOp(o);
                    // cari posisi call terkini (instruksi bergeser oleh
                    // sisipan sebelumnya).
                    std::size_t curPos = pos;
                    {
                        const IrBlock& B2 = fn.blocks[b];
                        while (curPos < B2.insts.size() &&
                               B2.insts[curPos] != iid) {
                            ++curPos;
                        }
                    }
                    ValueId nv = InvalidValue;
                    if (!editor.insertInstrBefore(f, b, curPos, clone, nv).ok) {
                        result.ok = false;
                        return result;
                    }
                    const ValueId ov = valueOfInstr(cf, cid);
                    if (ov != InvalidValue) vmap[ov] = nv;
                }
                // hasil: nilai operand Ret callee -> dialihkan ke use SSA
                // hasil Call.
                const InstrId cterm = cb.insts.back();
                if (cterm < cf.instrs.size() &&
                    !cf.instrs[cterm].operands.empty() &&
                    cf.instrs[cterm].operands[0].kind ==
                        IrOperandKind::ValueV) {
                    const auto it = vmap.find(
                        cf.instrs[cterm].operands[0].id);
                    if (it != vmap.end()) {
                        const ValueId callV = valueOfInstr(fn, iid);
                        if (callV != InvalidValue) {
                            for (BlockId wb = 0; wb < fn.blocks.size();
                                 ++wb) {
                                IrBlock& wblk = fn.blocks[wb];
                                for (std::size_t wp = 0;
                                     wp < wblk.insts.size(); ++wp) {
                                    const InstrId wid = wblk.insts[wp];
                                    if (wid >= fn.instrs.size()) continue;
                                    const std::vector<IrOperand> wops =
                                        fn.instrs[wid].operands;
                                    for (std::size_t woi = 0;
                                         woi < wops.size(); ++woi) {
                                        if (wops[woi].kind ==
                                                IrOperandKind::ValueV &&
                                            wops[woi].id == callV) {
                                            if (!editor.replaceOperandAt(
                                                    f, wb, wp, woi,
                                                    IrOperand::value(it->second)).ok) {
                                                result.ok = false;
                                                return result;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                dead[iid] = true;
                anyDead = true;
                inlineCount[c] += 1;
                result.folded += 1;
            }
        }
        if (anyDead) {
            if (!editor.eliminateInstrs(f, dead).ok) {
                result.ok = false;
                return result;
            }
            result.changed = true;
        }
    }
    return result;
}

} // namespace rhc
