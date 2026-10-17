#include "RhOptimizer/StructLayout.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"
#include "RhIR/IrType.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace rhc {

namespace {
} // namespace

IrPassResult StructLayoutOptimize::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    const std::uint32_t nT = static_cast<std::uint32_t>(module.types.size());
    // fmap[t][i] = indeks baru field i, atau ~0u bila mati.
    std::vector<std::vector<std::uint32_t>> fmap(nT);
    std::vector<std::uint8_t> touched(nT, 0);
    std::vector<IrType> newTypes;  // snapshot fields baru per tipe tersentuh

    // ---- fase 1: guard + liveness per tipe Struct ----
    for (TypeId t = 0; t < nT; ++t) {
        const IrType& ty = module.types[t];
        if (ty.kind != IrTypeKind::Struct || ty.fields.empty()) continue;
        bool bitfield = false;
        for (const IrField& f : ty.fields) {
            if (f.bitWidth != 0) {
                bitfield = true;
                break;
            }
        }
        if (bitfield) continue;
        fmap[t].assign(ty.fields.size(), 0);
        touched[t] = 1;
    }
    for (FuncId f = 0; f < module.functions.size(); ++f) {
        const IrFunction& fn = module.functions[f];
        for (const IrBlock& blk : fn.blocks) {
            for (const InstrId iid : blk.insts) {
                if (iid >= fn.instrs.size()) continue;
                const IrInstr& ins = fn.instrs[iid];
                // Tipe struct yang diakses: Extract -> tipe BASE
                // (operands[0] value; resultType = tipe FIELD);
                // Insert/Construct -> resultType (hasil = struct).
                TypeId rt = InvalidType;
                if (ins.op == IrOpcode::Extract) {
                    if (ins.operands.empty() ||
                        ins.operands[0].kind != IrOperandKind::ValueV ||
                        ins.operands[0].id >= fn.values.size()) {
                        continue;
                    }
                    rt = fn.values[ins.operands[0].id].type;
                } else if (ins.op == IrOpcode::Insert ||
                           ins.op == IrOpcode::Construct) {
                    rt = ins.resultType;
                } else {
                    continue;
                }
                if (rt >= nT || !touched[rt]) continue;
                if (ins.op == IrOpcode::Construct) {
                    // Construct: tidak menjaga field hidup (operand
                    // positional tak terbaca bila field mati), tapi
                    // dicatat untuk remap posisi nanti.
                    continue;
                }
                // idx = operands[1]; harus ImmV.
                if (ins.operands.size() < 2 ||
                    ins.operands[1].kind != IrOperandKind::ImmV) {
                    // idx dinamis -> tipe ini tidak disentuh sama sekali
                    touched[rt] = 0;
                    continue;
                }
                const std::uint32_t idx = ins.operands[1].id;
                if (idx >= fmap[rt].size()) {
                    touched[rt] = 0;
                    continue;
                }
                fmap[rt][idx] = 1;  // field hidup
            }
        }
    }

    // ---- fase 2: DFE + reorder untuk tipe tersentuh ----
    for (TypeId t = 0; t < nT; ++t) {
        if (!touched[t]) continue;
        IrType& ty = module.types[t];
        std::vector<std::uint32_t> alive;
        for (std::uint32_t i = 0; i < ty.fields.size(); ++i) {
            if (fmap[t][i]) alive.push_back(i);
        }
        if (alive.empty()) {
            // Semua field mati: DFE penuh menghasilkan struct 0 field
            // (ukuran 0) yang ditolak LayoutEngine bila jadi elemen
            // array (matriks.rhc dsb). Konservatif: tipe dibiarkan utuh.
            touched[t] = 0;
            continue;
        }
        // reorder by (alignBytes desc, sizeBytes desc) -- gunakan tabel
        // tipe untuk align/size field; fallback align=size=1.
        auto alignOf = [&module](TypeId ft) -> std::uint32_t {
            return (ft != InvalidType && ft < module.types.size())
                       ? module.types[ft].alignBytes
                       : 1;
        };
        auto sizeOf = [&module](TypeId ft) -> std::uint32_t {
            return (ft != InvalidType && ft < module.types.size())
                       ? module.types[ft].sizeBytes
                       : 1;
        };
        std::stable_sort(alive.begin(), alive.end(),
                         [&](std::uint32_t a, std::uint32_t b) {
                             const std::uint32_t aa = alignOf(ty.fields[a].type);
                             const std::uint32_t ba = alignOf(ty.fields[b].type);
                             if (aa != ba) return aa > ba;
                             return sizeOf(ty.fields[a].type) >
                                    sizeOf(ty.fields[b].type);
                         });
        std::vector<IrField> newFields;
        newFields.reserve(alive.size());
        std::vector<std::uint32_t> map2(ty.fields.size(), ~0u);
        for (std::uint32_t ni = 0; ni < alive.size(); ++ni) {
            map2[alive[ni]] = ni;
            IrField nf = ty.fields[alive[ni]];
            nf.offsetBytes = 0;  // dihitung ulang di bawah
            newFields.push_back(nf);
        }
        ty.fields = std::move(newFields);
        fmap[t] = std::move(map2);
        // offset System V sederhana.
        std::uint32_t offset = 0;
        std::uint32_t maxAlign = 1;
        for (IrField& fld : ty.fields) {
            const std::uint32_t a = alignOf(fld.type);
            const std::uint32_t s = sizeOf(fld.type);
            if (a == 0) continue;
            if (offset % a != 0) offset += a - (offset % a);
            fld.offsetBytes = offset;
            offset += s;
            if (a > maxAlign) maxAlign = a;
        }
        if (offset % maxAlign != 0) offset += maxAlign - (offset % maxAlign);
        ty.sizeBytes = offset;
        ty.alignBytes = maxAlign;
    }

    // ---- fase 3: remap idx Extract/Insert + Construct, module-wide ----
    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            IrBlock& blk = fn.blocks[b];
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid >= fn.instrs.size()) continue;
                const IrInstr ins = fn.instrs[iid];  // snapshot
                TypeId rt = InvalidType;
                if (ins.op == IrOpcode::Extract) {
                    if (ins.operands.empty() ||
                        ins.operands[0].kind != IrOperandKind::ValueV ||
                        ins.operands[0].id >= fn.values.size()) {
                        continue;
                    }
                    rt = fn.values[ins.operands[0].id].type;
                } else if (ins.op == IrOpcode::Insert ||
                           ins.op == IrOpcode::Construct) {
                    rt = ins.resultType;
                } else {
                    continue;
                }
                if (rt >= nT || !touched[rt]) continue;
                if (ins.op == IrOpcode::Extract || ins.op == IrOpcode::Insert) {
                    if (ins.operands.size() < 2 ||
                        ins.operands[1].kind != IrOperandKind::ImmV) {
                        continue;
                    }
                    const std::uint32_t oldIdx = ins.operands[1].id;
                    if (oldIdx >= fmap[rt].size() ||
                        fmap[rt][oldIdx] == ~0u ||
                        fmap[rt][oldIdx] == oldIdx) {  // identity: skip
                        continue;
                    }
                    if (!editor.replaceOperandAt(
                            f, b, pos, 1,
                            IrOperand::imm(fmap[rt][oldIdx])).ok) {
                        result.ok = false;
                        return result;
                    }
                    result.folded += 1;
                } else if (ins.op == IrOpcode::Construct) {
                    // operands[0]=TypeV; sisanya positional per field lama.
                    const std::size_t nOld = fmap[rt].size();
                    if (ins.operands.size() != 1 + nOld) continue;
                    IrInstr repl = ins;
                    std::vector<IrOperand> newOps;
                    newOps.push_back(ins.operands[0]);
                    for (std::uint32_t oldIdx = 0; oldIdx < nOld;
                         ++oldIdx) {
                        const std::uint32_t ni = fmap[rt][oldIdx];
                        if (ni == ~0u) continue;  // field mati dibuang
                        newOps.push_back(ins.operands[1 + oldIdx]);
                    }
                    repl.operands = std::move(newOps);
                    // identity check manual (IrOperand tak punya operator==).
                    bool identity = repl.operands.size() == ins.operands.size();
                    if (identity) {
                        for (std::size_t k = 0; k < repl.operands.size(); ++k) {
                            if (repl.operands[k].kind != ins.operands[k].kind ||
                                repl.operands[k].id != ins.operands[k].id) {
                                identity = false;
                                break;
                            }
                        }
                    }
                    if (identity) continue;
                    if (!editor.replaceInstr(f, b, pos, repl).ok) {
                        result.ok = false;
                        return result;
                    }
                    result.folded += 1;
                }
            }
        }
    }
    if (result.folded > 0) result.changed = true;
    return result;
}

} // namespace rhc
