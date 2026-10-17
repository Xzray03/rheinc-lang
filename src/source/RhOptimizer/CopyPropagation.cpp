#include "RhOptimizer/CopyPropagation.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <vector>

namespace rhc {

namespace {

// Resolve operand -> nilai integer konstan (ImmV langsung atau ValueV yang
// didefinisikan ConstInt/ConstBool), seperti resolver di pass lain.
bool constImm(const IrFunction& fn, const IrOperand& op, std::uint32_t& out) {
    if (op.kind == IrOperandKind::ImmV) {
        out = op.id;
        return true;
    }
    if (op.kind == IrOperandKind::ValueV && op.id < fn.values.size()) {
        const IrValue& v = fn.values[op.id];
        if (v.kind == IrValue::Kind::Instr && v.payload < fn.instrs.size()) {
            const IrInstr& d = fn.instrs[v.payload];
            if ((d.op == IrOpcode::ConstInt || d.op == IrOpcode::ConstBool) &&
                d.operands.size() == 1 &&
                d.operands[0].kind == IrOperandKind::ImmV) {
                out = d.operands[0].id;
                return true;
            }
        }
    }
    return false;
}

// Apakah instruksi berbentuk identitas copy? Bila ya, isi srcV dengan
// SSA operand sumber (v) dan kembalikan true.
bool identityCopy(const IrFunction& fn, const IrModule& module,
                  const IrInstr& instr, ValueId& srcV) {
    if (instr.operands.size() != 2) return false;
    // satu operand ValueV (sumber), satu operand konstan (identitas)
    int vIdx = -1;
    for (std::uint32_t i = 0; i < 2; ++i) {
        if (instr.operands[i].kind == IrOperandKind::ValueV &&
            instr.operands[1 - i].kind != IrOperandKind::ValueV) {
            vIdx = static_cast<int>(i);
            break;
        }
    }
    if (vIdx < 0) return false;
    // kandidat op identitas; konstanta identitas tergantung op
    const IrOpcode op = instr.op;
    std::uint32_t c = 0;
    if (!constImm(fn, instr.operands[1 - vIdx], c)) return false;
    bool ident = false;
    switch (op) {
    case IrOpcode::Add:
    case IrOpcode::Sub:
    case IrOpcode::BitOr:
    case IrOpcode::BitXor:
    case IrOpcode::Shl:
    case IrOpcode::Shr:
        ident = (c == 0);
        break;
    case IrOpcode::Mul:
        ident = (c == 1);
        break;
    case IrOpcode::BitAnd: {
        // mask penuh lebar tipe: hanya Int dengan sizeBytes 1/2/4
        // (8 byte: mask ~0 tidak representabel ImmV 32-bit, tapi c
        // konstan 32-bit tak mungkin == mask 64-bit kecuali 0xFFFFFFFF
        // yang BUKAN mask penuh 8 byte -> aman otomatis).
        if (instr.resultType == InvalidType ||
            instr.resultType >= module.types.size()) {
            break;
        }
        const IrType& t = module.types[instr.resultType];
        if (t.kind != IrTypeKind::Int || t.sizeBytes >= 8) break;
        const std::uint32_t bits = t.sizeBytes * 8;
        const std::uint32_t mask =
            bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
        ident = (c == mask);
        break;
    }
    default:
        break;
    }
    if (!ident) return false;
    srcV = instr.operands[vIdx].id;
    return true;
}

} // namespace

IrPassResult CopyPropagation::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            IrBlock& blk = fn.blocks[b];
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid == InvalidInstr || iid >= fn.instrs.size()) continue;
                const IrInstr instr = fn.instrs[iid];  // snapshot
                ValueId srcV = InvalidValue;
                if (!identityCopy(fn, module, instr, srcV)) continue;
                if (srcV >= fn.values.size()) continue;
                // SSA value hasil instruksi copy.
                const ValueId dstV = [&fn, iid]() -> ValueId {
                    for (std::uint32_t v = 0; v < fn.values.size(); ++v) {
                        if (fn.values[v].kind == IrValue::Kind::Instr &&
                            fn.values[v].payload == iid) {
                            return v;
                        }
                    }
                    return InvalidValue;
                }();
                if (dstV == InvalidValue) continue;
                // Alihkan semua USE dstV -> srcV di seluruh fungsi.
                const std::uint32_t foldedBefore = result.folded;
                for (BlockId ub = 0; ub < fn.blocks.size(); ++ub) {
                    IrBlock& ublk = fn.blocks[ub];
                    for (std::size_t upos = 0; upos < ublk.insts.size();
                         ++upos) {
                        const InstrId uiid = ublk.insts[upos];
                        if (uiid >= fn.instrs.size()) continue;
                        const std::vector<IrOperand> uops =
                            fn.instrs[uiid].operands;  // snapshot
                        for (std::size_t uoi = 0; uoi < uops.size(); ++uoi) {
                            if (uops[uoi].kind == IrOperandKind::ValueV &&
                                uops[uoi].id == dstV) {
                                if (!editor.replaceOperandAt(
                                        f, ub, upos, uoi,
                                        IrOperand::value(srcV)).ok) {
                                    result.ok = false;
                                    return result;
                                }
                                result.folded += 1;
                            }
                        }
                    }
                }
                // changed hanya bila benar-benar ada USE yang terganti
                // (copy zero-use setelah putaran pertama tidak dianggap
                // perubahan -- konsisten dengan epoch change-detection net).
                if (result.folded > foldedBefore) result.changed = true;
            }
        }
    }
    return result;
}

} // namespace rhc
