#include "RhOptimizer/InlineFunctions.h"

#include "RhIR/IrConstant.h"
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

    // kandidat callee per FunId. Callee harus satu blok, kecil, berakhir
    // Ret, dan tak rekursif. Phi tidak didukung (clone phi mentah ke
    // posisi non-header menghasilkan IR invalid) -> skip.
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
        for (const IrInstr& ci : cf.instrs) {
            if (ci.op == IrOpcode::Phi) return false;
        }
        return true;
    };

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        std::vector<bool> dead(fn.instrs.size(), false);
        bool anyDead = false;
        std::unordered_map<FuncId, std::uint32_t> inlineCount;

        // [FIX] SNAPSHOT PRE-PASS: kumpulkan semua call site eligible TANPA
        // mutasi lebih dulu. Clone yang disisipkan saat processing tidak
        // pernah ada di snapshot, sehingga tidak ada cascade inline dan
        // tidak ada pergeseran indeks blk.insts saat iterasi (akar bug
        // forward-reference/%v? pada IR dengan banyak value/phi).
        struct Site {
            BlockId b;
            InstrId iid;
            FuncId callee;
        };
        std::vector<Site> sites;
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& blk = fn.blocks[b];
            for (const InstrId iid : blk.insts) {
                if (iid >= fn.instrs.size()) continue;
                const IrInstr& callI = fn.instrs[iid];
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
                // Argumen harus bisa di-remap: ValueV langsung, ImmV atau
                // ConstantV string (di-materialisasi jadi instruksi
                // konstanta saat processing). Kind lain -> skip.
                bool argsOk = true;
                for (std::size_t pi = 0; pi < cf.params.size(); ++pi) {
                    const IrOperand& a = callI.operands[1 + pi];
                    if (a.kind == IrOperandKind::ValueV) continue;
                    if (a.kind == IrOperandKind::ImmV) continue;
                    if (a.kind == IrOperandKind::ConstantV &&
                        a.id < module.constants.size() &&
                        module.constants[a.id].kind ==
                            IrConstantKind::String) {
                        continue;
                    }
                    argsOk = false;
                    break;
                }
                if (!argsOk) continue;
                inlineCount[c] += 1;
                sites.push_back({b, iid, c});
            }
        }

        // Proses tiap call site dari snapshot.
        for (const Site& site : sites) {
            const InstrId iid = site.iid;
            if (iid >= fn.instrs.size()) continue;
            const IrInstr callI = fn.instrs[iid];  // snapshot
            const IrFunction& cf = module.functions[site.callee];

            // Relokasi posisi call saat ini dalam blok (instruksi bisa
            // bergeser oleh sisipan site sebelumnya).
            auto locateCall = [&]() -> std::size_t {
                const IrBlock& B2 = fn.blocks[site.b];
                for (std::size_t p = 0; p < B2.insts.size(); ++p) {
                    if (B2.insts[p] == iid) return p;
                }
                return B2.insts.size();
            };

            std::unordered_map<ValueId, ValueId> vmap;
            // Binding param -> arg; materialisasi arg non-ValueV tepat
            // sebelum call (tipe dari param callee).
            for (std::size_t pi = 0; pi < cf.params.size(); ++pi) {
                const IrOperand& a = callI.operands[1 + pi];
                if (a.kind == IrOperandKind::ValueV) {
                    vmap[cf.params[pi]] = a.id;
                    continue;
                }
                const TypeId pt = cf.values[cf.params[pi]].type;
                IrInstr mat{};
                mat.resultType = pt;
                if (a.kind == IrOperandKind::ImmV) {
                    const bool isBool =
                        pt < module.types.size() &&
                        module.types[pt].kind == IrTypeKind::Bool;
                    mat.op = isBool ? IrOpcode::ConstBool : IrOpcode::ConstInt;
                    mat.operands = {IrOperand::imm(a.id)};
                } else {  // ConstantV string
                    mat.op = IrOpcode::ConstString;
                    mat.operands = {IrOperand::constant(a.id)};
                }
                const std::size_t curPos = locateCall();
                if (curPos >= fn.blocks[site.b].insts.size()) {
                    result.ok = false;
                    return result;
                }
                ValueId mv = InvalidValue;
                if (!editor.insertInstrBefore(f, site.b, curPos, mat, mv).ok) {
                    result.ok = false;
                    return result;
                }
                vmap[cf.params[pi]] = mv;
            }

            auto remapOp = [&](const IrOperand& op) -> IrOperand {
                if (op.kind != IrOperandKind::ValueV) return op;
                const auto it = vmap.find(op.id);
                if (it != vmap.end()) return IrOperand::value(it->second);
                return op;
            };

            // Clone instruksi callee (semua kecuali terminator), sisipkan
            // tepat sebelum call.
            const IrBlock& cb = cf.blocks[0];
            for (std::size_t ci = 0; ci + 1 < cb.insts.size(); ++ci) {
                const InstrId cid = cb.insts[ci];
                if (cid >= cf.instrs.size()) continue;
                IrInstr clone = cf.instrs[cid];  // snapshot
                for (IrOperand& o : clone.operands) o = remapOp(o);
                const std::size_t curPos = locateCall();
                if (curPos >= fn.blocks[site.b].insts.size()) {
                    result.ok = false;
                    return result;
                }
                ValueId nv = InvalidValue;
                if (!editor.insertInstrBefore(f, site.b, curPos, clone, nv).ok) {
                    result.ok = false;
                    return result;
                }
                const ValueId ov = valueOfInstr(cf, cid);
                if (ov != InvalidValue) vmap[ov] = nv;
            }

            // Hasil: nilai operand Ret callee -> dialihkan ke use SSA hasil
            // Call di caller.
            const InstrId cterm = cb.insts.back();
            if (cterm < cf.instrs.size() &&
                !cf.instrs[cterm].operands.empty() &&
                cf.instrs[cterm].operands[0].kind ==
                    IrOperandKind::ValueV) {
                const auto it = vmap.find(cf.instrs[cterm].operands[0].id);
                if (it != vmap.end()) {
                    const ValueId callV = valueOfInstr(fn, iid);
                    if (callV != InvalidValue) {
                        for (BlockId wb = 0; wb < fn.blocks.size(); ++wb) {
                            IrBlock& wblk = fn.blocks[wb];
                            for (std::size_t wp = 0;
                                 wp < wblk.insts.size(); ++wp) {
                                const InstrId wid = wblk.insts[wp];
                                if (wid >= fn.instrs.size()) continue;
                                const std::vector<IrOperand> wops =
                                    fn.instrs[wid].operands;
                                for (std::size_t woi = 0; woi < wops.size();
                                     ++woi) {
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
            result.folded += 1;
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
