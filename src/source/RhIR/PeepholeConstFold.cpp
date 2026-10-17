#include "RhIR/PeepholeConstFold.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>

namespace rhc {

namespace {

// Floor division (pembulatan ke -inf), sesuai semantik DivFloor bahasa.
std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    const std::int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) --q;
    return q;
}

std::int64_t floorMod(std::int64_t a, std::int64_t b) {
    return a - floorDiv(a, b) * b;
}

} // namespace

IrPassResult PeepholeConstFold::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& blk = fn.blocks[b];
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid == InvalidInstr || iid >= fn.instrs.size()) continue;
                const IrInstr& instr = fn.instrs[iid];

                // Resolusi operand -> nilai integer konstan:
                // ImmV langsung, atau ValueV yang didefinisikan oleh
                // ConstInt(ImmV)/ConstBool(ImmV) pada fungsi yang sama.
                auto resolveImm = [&fn](const IrOperand& op,
                                        std::uint32_t& out) -> bool {
                    if (op.kind == IrOperandKind::ImmV) {
                        out = op.id;
                        return true;
                    }
                    if (op.kind != IrOperandKind::ValueV ||
                        op.id >= fn.values.size()) {
                        return false;
                    }
                    const IrValue& v = fn.values[op.id];
                    if (v.kind != IrValue::Kind::Instr ||
                        v.payload >= fn.instrs.size()) {
                        return false;
                    }
                    const IrInstr& def = fn.instrs[v.payload];
                    if ((def.op != IrOpcode::ConstInt &&
                         def.op != IrOpcode::ConstBool) ||
                        def.operands.size() != 1 ||
                        def.operands[0].kind != IrOperandKind::ImmV) {
                        return false;
                    }
                    out = def.operands[0].id;
                    return true;
                };
                // Resolve khusus boolean: hanya dari ConstBool.
                auto resolveBool = [&fn](const IrOperand& op,
                                         bool& out) -> bool {
                    if (op.kind != IrOperandKind::ValueV ||
                        op.id >= fn.values.size()) {
                        return false;
                    }
                    const IrValue& v = fn.values[op.id];
                    if (v.kind != IrValue::Kind::Instr ||
                        v.payload >= fn.instrs.size()) {
                        return false;
                    }
                    const IrInstr& def = fn.instrs[v.payload];
                    if (def.op != IrOpcode::ConstBool ||
                        def.operands.size() != 1 ||
                        def.operands[0].kind != IrOperandKind::ImmV) {
                        return false;
                    }
                    out = def.operands[0].id != 0;
                    return true;
                };

                // Signedness hasil untuk aritmetika/pembagian/compare.
                const bool sgn =
                    instr.resultType != InvalidType &&
                    instr.resultType < module.types.size() &&
                    module.types[instr.resultType].isSigned;

                IrInstr foldedInstr = instr;
                bool folded = false;
                bool asBool = false;
                bool boolVal = false;
                std::uint32_t intVal = 0;

                if (instr.operands.size() == 2) {
                    std::uint32_t lhs = 0, rhs = 0;
                    if (!resolveImm(instr.operands[0], lhs) ||
                        !resolveImm(instr.operands[1], rhs)) {
                        continue;
                    }
                    const std::int64_t sL =
                        sgn ? static_cast<std::int64_t>(
                                  static_cast<std::int32_t>(lhs))
                            : static_cast<std::int64_t>(lhs);
                    const std::int64_t sR =
                        sgn ? static_cast<std::int64_t>(
                                  static_cast<std::int32_t>(rhs))
                            : static_cast<std::int64_t>(rhs);
                    switch (instr.op) {
                    case IrOpcode::Add:
                        intVal = lhs + rhs;
                        break;
                    case IrOpcode::Sub:
                        intVal = lhs - rhs;
                        break;
                    case IrOpcode::Mul:
                        intVal = lhs * rhs;
                        break;
                    case IrOpcode::Div:
                    case IrOpcode::DivFloor:
                        if (rhs == 0) continue;  // biarkan runtime memutuskan
                        intVal = static_cast<std::uint32_t>(
                            sgn ? floorDiv(sL, sR) : sL / sR);
                        break;
                    case IrOpcode::Mod:
                        if (rhs == 0) continue;
                        intVal = static_cast<std::uint32_t>(
                            sgn ? floorMod(sL, sR) : sL % sR);
                        break;
                    case IrOpcode::Pow: {
                        if (rhs > 10000) continue;  // batas loop
                        std::uint32_t acc = 1;
                        for (std::uint32_t i = 0; i < rhs; ++i) {
                            acc = acc * lhs;  // wrap-around sesuai tipe
                        }
                        intVal = acc;
                        break;
                    }
                    case IrOpcode::BitAnd: intVal = lhs & rhs; break;
                    case IrOpcode::BitOr:  intVal = lhs | rhs; break;
                    case IrOpcode::BitXor: intVal = lhs ^ rhs; break;
                    case IrOpcode::Shl:
                        if (rhs >= 32) continue;  // UB, biarkan runtime
                        intVal = lhs << rhs;
                        break;
                    case IrOpcode::Shr:
                        if (rhs >= 32) continue;
                        intVal = sgn ? static_cast<std::uint32_t>(
                                           static_cast<std::int32_t>(lhs) >>
                                           rhs)
                                     : lhs >> rhs;
                        break;
                    case IrOpcode::CmpEq:
                        asBool = true;
                        boolVal = lhs == rhs;
                        break;
                    case IrOpcode::CmpNe:
                        asBool = true;
                        boolVal = lhs != rhs;
                        break;
                    case IrOpcode::CmpLt:
                        asBool = true;
                        boolVal = sL < sR;
                        break;
                    case IrOpcode::CmpLe:
                        asBool = true;
                        boolVal = sL <= sR;
                        break;
                    case IrOpcode::CmpGt:
                        asBool = true;
                        boolVal = sL > sR;
                        break;
                    case IrOpcode::CmpGe:
                        asBool = true;
                        boolVal = sL >= sR;
                        break;
                    default:
                        continue;  // op lain: belum didukung fold
                    }
                    folded = true;
                    foldedInstr.operands = {
                        IrOperand::imm(asBool ? (boolVal ? 1u : 0u)
                                              : intVal)};
                    foldedInstr.op =
                        asBool ? IrOpcode::ConstBool : IrOpcode::ConstInt;
                    foldedInstr.name.clear();
                } else if (instr.operands.size() == 1) {
                    if (instr.op == IrOpcode::Neg) {
                        std::uint32_t x = 0;
                        if (!resolveImm(instr.operands[0], x)) continue;
                        intVal = 0u - x;  // wrap-around
                        folded = true;
                        foldedInstr.op = IrOpcode::ConstInt;
                        foldedInstr.operands = {IrOperand::imm(intVal)};
                        foldedInstr.name.clear();
                    } else if (instr.op == IrOpcode::BitNot) {
                        std::uint32_t x = 0;
                        if (!resolveImm(instr.operands[0], x)) continue;
                        intVal = ~x;
                        folded = true;
                        foldedInstr.op = IrOpcode::ConstInt;
                        foldedInstr.operands = {IrOperand::imm(intVal)};
                        foldedInstr.name.clear();
                    } else if (instr.op == IrOpcode::Not) {
                        bool x = false;
                        if (!resolveBool(instr.operands[0], x)) continue;
                        asBool = true;
                        boolVal = !x;
                        folded = true;
                        foldedInstr.op = IrOpcode::ConstBool;
                        foldedInstr.operands = {
                            IrOperand::imm(boolVal ? 1u : 0u)};
                        foldedInstr.name.clear();
                    }
                }
                if (!folded) continue;

                if (!editor.replaceInstr(f, b, pos, foldedInstr).ok) {
                    result.ok = false;
                    return result;
                }
                result.changed = true;
                result.folded += 1;
            }
        }
    }
    return result;
}

} // namespace rhc
