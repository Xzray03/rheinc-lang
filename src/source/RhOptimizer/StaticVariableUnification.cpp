#include "RhOptimizer/StaticVariableUnification.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace rhc {

namespace {

// Resolve operand -> konstanta: int/bool -> ImmV (outBits + isFloat=false);
// desimal -> FloatBits 64 (outBits + isFloat=true, butuh entri pool).
enum class KonstKind { None, Imm, FloatBits };
KonstKind constOf(const IrFunction& initFn, const IrOperand& op,
                  std::uint64_t& bits) {
    if (op.kind == IrOperandKind::ImmV) {
        bits = op.id;
        return KonstKind::Imm;
    }
    if (op.kind == IrOperandKind::ValueV && op.id < initFn.values.size()) {
        const IrValue& v = initFn.values[op.id];
        if (v.kind != IrValue::Kind::Instr ||
            v.payload >= initFn.instrs.size()) {
            return KonstKind::None;
        }
        const IrInstr& d = initFn.instrs[v.payload];
        if (d.op == IrOpcode::ConstInt) {
            // 1-2 ImmV (lo, hi) untuk nilai 64-bit (DEVIATIONS D-4).
            return decodeConstInt(d, bits) ? KonstKind::Imm
                                           : KonstKind::None;
        }
        if (d.operands.size() != 1) return KonstKind::None;
        if (d.op == IrOpcode::ConstBool) {
            if (d.operands[0].kind != IrOperandKind::ImmV) {
                return KonstKind::None;
            }
            bits = d.operands[0].id;
            return KonstKind::Imm;
        }
        if (d.op == IrOpcode::ConstFloat && d.operands.size() == 2 &&
            d.operands[0].kind == IrOperandKind::ImmV &&
            d.operands[1].kind == IrOperandKind::ImmV) {
            bits = static_cast<std::uint64_t>(d.operands[0].id) |
                   (static_cast<std::uint64_t>(d.operands[1].id) << 32);
            return KonstKind::FloatBits;
        }
        if (d.op == IrOpcode::ConstFloat && d.operands.size() == 1 &&
            d.operands[0].kind == IrOperandKind::ImmV) {
            // f32 lama (defensif; bahasa hanya f64): expand
            const std::uint32_t lo = d.operands[0].id;
            float fv;
            std::memcpy(&fv, &lo, sizeof(fv));
            double dv = static_cast<double>(fv);
            std::memcpy(&bits, &dv, sizeof(bits));
            return KonstKind::FloatBits;
        }
    }
    return KonstKind::None;
}

} // namespace

IrPassResult StaticVariableUnification::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    // ---- fase 1: hitung Store[GlobalV] per slot, seluruh modul ----
    const std::uint32_t nG =
        static_cast<std::uint32_t>(module.globalVars.size());
    std::vector<std::uint32_t> storeCount(nG, 0);
    std::vector<InstrId> storeInstr(nG, InvalidInstr);
    std::vector<FuncId> storeFunc(nG, InvalidFunc);
    for (FuncId f = 0; f < module.functions.size(); ++f) {
        const IrFunction& fn = module.functions[f];
        for (const IrBlock& blk : fn.blocks) {
            for (const InstrId iid : blk.insts) {
                if (iid >= fn.instrs.size()) continue;
                const IrInstr& ins = fn.instrs[iid];
                if (ins.op != IrOpcode::Store || ins.operands.empty() ||
                    ins.operands[0].kind != IrOperandKind::GlobalV ||
                    ins.operands[0].id >= nG) {
                    continue;
                }
                const std::uint32_t g = ins.operands[0].id;
                storeCount[g] += 1;
                storeInstr[g] = iid;
                storeFunc[g] = f;
            }
        }
    }

    // ---- fase 2: kumpulkan kandidat (indeks g asli; kompaksi globalVars
    // hanya SEKALI di akhir agar indeks tidak bergeser antar-kandidat) ----
    struct Cand {
        std::uint32_t g;
        KonstKind kind;
        std::uint64_t bits;
    };
    std::vector<Cand> cands;
    for (std::uint32_t g = 0; g < nG; ++g) {
        if (storeCount[g] != 1) continue;
        const IrFunction& initFn = module.functions[storeFunc[g]];
        if (storeFunc[g] == InvalidFunc ||
            initFn.kind != IrFunctionKind::GlobalInit) {
            continue;
        }
        const InstrId sid = storeInstr[g];
        if (sid >= initFn.instrs.size()) continue;
        const IrInstr storeI = initFn.instrs[sid];  // snapshot
        if (storeI.operands.size() < 2) continue;
        std::uint64_t bits = 0;
        const KonstKind kk = constOf(initFn, storeI.operands[1], bits);
        if (kk == KonstKind::None) continue;
        cands.push_back({g, kk, bits});
    }

    // ---- fase 3..5: eksekusi per kandidat ----
    std::vector<bool> deadG(nG, false);
    bool anySlot = false;
    for (const Cand& cd : cands) {
        const std::uint32_t g = cd.g;
        const KonstKind kk = cd.kind;
        const std::uint64_t bits = cd.bits;

        // Bangun operand pengganti: Imm langsung, atau ConstantV
        // (intern FloatBits ke pool dengan dedup).
        IrOperand replacement;
        if (kk == KonstKind::Imm) {
            replacement = IrOperand::imm(static_cast<std::uint32_t>(bits));
        } else {
            const TypeId gType = module.globalVars[g].type;
            ConstId cid = InvalidConst;
            bool found = false;
            for (ConstId i = 0; i < module.constants.size(); ++i) {
                const IrConstant& c = module.constants[i];
                if (c.kind == IrConstantKind::FloatBits &&
                    c.type == gType && c.bitPattern == bits) {
                    cid = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                IrConstant c;
                c.kind = IrConstantKind::FloatBits;
                c.type = gType;
                c.bitPattern = bits;
                if (!editor.appendConst(c, cid).ok) {
                    result.ok = false;
                    return result;
                }
            }
            replacement = IrOperand::constant(cid);
        }

        // ---- fase 3: rewrite semua Load @G -> konstanta ----
        std::uint32_t promoted = 0;
        for (FuncId f = 0; f < module.functions.size(); ++f) {
            IrFunction& fn = module.functions[f];
            for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                IrBlock& blk = fn.blocks[b];
                for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                    const InstrId lid = blk.insts[pos];
                    if (lid >= fn.instrs.size()) continue;
                    const IrInstr li = fn.instrs[lid];  // snapshot
                    if (li.op != IrOpcode::Load || li.operands.empty() ||
                        li.operands[0].kind != IrOperandKind::GlobalV ||
                        li.operands[0].id != g) {
                        continue;
                    }
                    // alihkan semua use SSA hasil Load -> replacement.
                    std::uint32_t loadV = InvalidValue;
                    for (std::uint32_t v = 0; v < fn.values.size(); ++v) {
                        if (fn.values[v].kind == IrValue::Kind::Instr &&
                            fn.values[v].payload == lid) {
                            loadV = v;
                            break;
                        }
                    }
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
                                            f, wb, wp, woi, replacement).ok) {
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
        }

        deadG[g] = true;
        anySlot = true;
        result.changed = true;
        result.folded += promoted + 2;
    }
    // ---- fase 5: ELIMINASI instruksi SEKALI untuk semua kandidat.
    // Scan ulang berbasis deadG -- TANPA cache InstrId (tabel instruksi
    // masih stabil: belum ada pemadatan sebelum titik ini). Semua
    // Load/Store ke slot kandidat dibuang dulu -> invariant
    // removeDeadGlobals ("tak ada operand GlobalV ke slot terhapus") terpenuhi.
    if (anySlot) {
        for (FuncId f = 0; f < module.functions.size(); ++f) {
            IrFunction& fn = module.functions[f];
            std::vector<bool> fdead(fn.instrs.size(), false);
            bool any = false;
            for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                for (const InstrId iid : fn.blocks[b].insts) {
                    if (iid >= fn.instrs.size()) continue;
                    const IrInstr& ins = fn.instrs[iid];
                    if (ins.operands.empty() ||
                        ins.operands[0].kind != IrOperandKind::GlobalV ||
                        ins.operands[0].id >= deadG.size() ||
                        !deadG[ins.operands[0].id]) {
                        continue;
                    }
                    if (ins.op == IrOpcode::Load ||
                        ins.op == IrOpcode::Store) {
                        fdead[iid] = true;
                        any = true;
                    }
                }
            }
            if (any && !editor.eliminateInstrs(f, fdead).ok) {
                result.ok = false;
                return result;
            }
        }
    }
    // ---- fase 6: kompaksi slot SEKALI untuk semua kandidat ----
    if (anySlot && !editor.removeDeadGlobals(deadG).ok) {
        result.ok = false;
        return result;
    }
    return result;
}

} // namespace rhc
