#include "RhOptimizer/StrengthReduction.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <vector>

namespace rhc {

namespace {

// Resolve konstanta integer 64-bit dari operand: ImmV langsung atau
// ValueV yang didefinisikan ConstInt (1-2 ImmV lo/hi, DEVIATIONS D-4).
bool constIntOf(const IrFunction& fn, const IrOperand& op,
                std::uint64_t& out) {
    if (op.kind == IrOperandKind::ImmV) {
        out = op.id;
        return true;
    }
    if (op.kind != IrOperandKind::ValueV || op.id >= fn.values.size()) {
        return false;
    }
    const IrValue& v = fn.values[op.id];
    if (v.kind != IrValue::Kind::Instr || v.payload >= fn.instrs.size()) {
        return false;
    }
    return decodeConstInt(fn.instrs[v.payload], out);
}

bool isPow2(std::uint64_t v) { return v != 0 && (v & (v - 1)) == 0; }
std::uint32_t log2Of(std::uint64_t v) {
    std::uint32_t k = 0;
    while (v >>= 1) ++k;
    return k;
}

} // namespace

IrPassResult StrengthReduction::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            IrBlock& blk = fn.blocks[b];
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid == InvalidInstr || iid >= fn.instrs.size()) continue;
                // SNAPSHOT: insertInstrBefore melakukan push_back ke
                // fn.instrs/fn.values — referensi ke elemen vector tak
                // boleh dipegang melintasi pemanggilan editor.
                const IrInstr instr = fn.instrs[iid];  // salinan
                if (instr.operands.size() != 2) continue;

                const bool sgn =
                    instr.resultType != InvalidType &&
                    instr.resultType < module.types.size() &&
                    module.types[instr.resultType].isSigned;

                std::uint64_t cL = 0, cR = 0;
                const bool isCL = constIntOf(fn, instr.operands[0], cL);
                const bool isCR = constIntOf(fn, instr.operands[1], cR);
                if (isCL && isCR) continue;  // serahkan ke const fold

                if (!isCL && !isCR) {
                    // x+x -> Shl(x,1)
                    if (instr.op == IrOpcode::Add &&
                        instr.operands[0].kind == IrOperandKind::ValueV &&
                        instr.operands[1].kind == IrOperandKind::ValueV &&
                        instr.operands[0].id == instr.operands[1].id) {
                        IrInstr repl = instr;
                        repl.op = IrOpcode::Shl;
                        repl.operands = {instr.operands[0],
                                         IrOperand::imm(1)};
                        repl.name.clear();
                        if (!editor.replaceInstr(f, b, pos, repl).ok) {
                            result.ok = false; return result;
                        }
                        result.changed = true;
                        result.folded += 1;
                    }
                    continue;
                }

                const IrOpcode op = instr.op;
                if (op != IrOpcode::Mul && op != IrOpcode::Div &&
                    op != IrOpcode::DivFloor && op != IrOpcode::Mod &&
                    op != IrOpcode::Pow && op != IrOpcode::Add) {
                    continue;
                }
                const bool comm = op == IrOpcode::Add || op == IrOpcode::Mul;
                if (!isCR && !comm) continue;  // konstan di kiri non-komutatif
                IrOperand xOp =
                    isCR ? instr.operands[0] : instr.operands[1];
                const std::uint64_t c = isCR ? cR : cL;

                IrInstr repl = instr;
                repl.name.clear();
                bool done = false;
                auto emit2 = [&](IrOpcode newOp, const IrOperand& a,
                                 std::uint32_t immC) {
                    repl.op = newOp;
                    repl.operands = {a, IrOperand::imm(immC)};
                    done = true;
                };
                // Sisipkan instrugsi perantara sebelum pos, daftarkan
                // value-nya; operasi terakhir menggantikan instruksi
                // asli lewat repl.
                auto insertBefore = [&](IrOpcode newOp,
                                        const std::vector<IrOperand>& ops,
                                        ValueId& outV) -> bool {
                    IrInstr ni;
                    ni.op = newOp;
                    ni.resultType = instr.resultType;
                    ni.operands = ops;
                    return editor
                        .insertInstrBefore(f, b, pos, ni, outV)
                        .ok;
                };

                if (op == IrOpcode::Add) {
                    // konstan di kanan: tidak ada reduksi (identitas
                    // tidak direpresentasikan op khusus di IR ini).
                    continue;
                }
                if (op == IrOpcode::Mul) {
                    if (c == 0) { emit2(IrOpcode::BitAnd, xOp, 0); }
                    else if (c == 1) { emit2(IrOpcode::Shl, xOp, 0); }
                    else if (isPow2(c)) { emit2(IrOpcode::Shl, xOp, log2Of(c)); }
                    else {
                        // Dekomposisi shift-add: c = 2^k0 + 2^k1 + ...
                        std::vector<std::uint32_t> ks;
                        for (std::uint32_t k = 0; k < 64; ++k) {
                            if (c & (1ull << k)) ks.push_back(k);
                        }
                        // Optimasi: term 2^0 == x — pakai x langsung.
                        ValueId acc = InvalidValue;
                        bool accMat = false;
                        std::size_t i0 = 0;
                        if (ks[0] == 0) { accMat = false; i0 = 1; }
                        else {
                            if (!insertBefore(IrOpcode::Shl,
                                    {xOp, IrOperand::imm(ks[0])}, acc)) {
                                result.ok = false; return result;
                            }
                            accMat = true;
                        }
                        for (std::size_t i = i0; i + 1 < ks.size(); ++i) {
                            {
                                const IrOperand aOp = accMat
                                    ? IrOperand::value(acc) : xOp;
                                ValueId tv = InvalidValue;
                                if (!insertBefore(IrOpcode::Shl,
                                        {xOp, IrOperand::imm(ks[i])}, tv)) {
                                    result.ok = false; return result;
                                }
                                ValueId av = InvalidValue;
                                if (!insertBefore(IrOpcode::Add,
                                        {aOp, IrOperand::value(tv)}, av)) {
                                    result.ok = false; return result;
                                }
                                acc = av;
                                accMat = true;
                            }
                        }
                        ValueId lastT = InvalidValue;
                        if (!insertBefore(IrOpcode::Shl,
                                {xOp, IrOperand::imm(ks.back())}, lastT)) {
                            result.ok = false; return result;
                        }
                        repl.op = IrOpcode::Add;
                        repl.operands = {accMat ? IrOperand::value(acc) : xOp,
                                         IrOperand::value(lastT)};
                        done = true;
                    }
                } else if (op == IrOpcode::Div || op == IrOpcode::DivFloor) {
                    if (c == 0 || !isPow2(c)) continue;
                    const bool floorOk =
                        (op == IrOpcode::DivFloor) || !sgn;
                    if (!floorOk) continue;  // signed trunc-div bukan shift
                    emit2(IrOpcode::Shr, xOp, log2Of(c));
                } else if (op == IrOpcode::Mod) {
                    // unsigned; mask c-1 harus muat ImmV 32-bit
                    if (c == 0 || !isPow2(c) || sgn ||
                        c - 1 > 0xFFFFFFFFull) continue;
                    emit2(IrOpcode::BitAnd, xOp,
                          static_cast<std::uint32_t>(c - 1));
                } else {  // Pow
                    if (c < 2 || c > 16) continue;
                    // Rantai biner dari MSB: SQUARE / MULX. Operasi
                    // terakhir menjadi repl (bisa tanpa sisipan).
                    std::vector<bool> ops;  // true=SQUARE, false=MULX
                    const std::uint32_t bit =
                        63 - __builtin_clzll(c);
                    for (std::int32_t i = static_cast<std::int32_t>(bit) - 1;
                         i >= 0; --i) {
                        ops.push_back(true);
                        if ((c >> i) & 1u) ops.push_back(false);
                    }
                    ValueId acc = InvalidValue;
                    bool accMat = false;  // false: acc konseptual = x
                    for (std::size_t j = 0; j < ops.size(); ++j) {
                        const bool last = j + 1 == ops.size();
                        const IrOperand a = accMat
                            ? IrOperand::value(acc) : xOp;
                        IrOpcode newOp =
                            ops[j] ? IrOpcode::Mul : IrOpcode::Mul;
                        std::vector<IrOperand> o;
                        if (ops[j]) o = {a, a};        // SQUARE
                        else        o = {a, xOp};      // MULX
                        if (last) {
                            repl.op = newOp;
                            repl.operands = std::move(o);
                            done = true;
                        } else {
                            if (!insertBefore(newOp, o, acc)) {
                                result.ok = false; return result;
                            }
                            accMat = true;
                        }
                    }
                }
                if (!done) continue;
                // Sisipan menggeser instruksi asli ke kanan — temukan
                // posisi terkini iid (tetap unik dalam block) agar
                // replaceInstr mengganti instruksi yang benar.
                std::size_t curPos = pos;
                for (std::size_t i = 0; i < blk.insts.size(); ++i) {
                    if (blk.insts[i] == iid) { curPos = i; break; }
                }
                if (!editor.replaceInstr(f, b, curPos, repl).ok) {
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
