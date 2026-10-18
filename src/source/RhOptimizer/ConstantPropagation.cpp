#include "RhOptimizer/ConstantPropagation.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <optional>

namespace rhc {

namespace {

// Fakta konstanta per SSA value. Float diPROPAGASIKAN lewat jalur
// khusus: operand ValueV ditulis ulang menjadi ConstantV (referensi
// constant pool module, FloatBits 64-bit) — ImmV hanya 32-bit sehingga
// tak bisa membawa bit pattern desimal.
struct Fact {
    enum class K : std::uint8_t { Int, Bool, Float } k = K::Int;
    std::uint32_t bits = 0;      // Bool: 0/1
    std::uint64_t bits64 = 0;    // Int: bit pattern penuh (<=64 bit);
                                 // Float: IEEE-754 double 64-bit
    TypeId type = InvalidType;   // Float: tipe desimal hasil
};

bool factFromDef(const IrInstr& def, Fact& out) {
    if (def.operands.empty() ||
        def.operands[0].kind != IrOperandKind::ImmV) {
        return false;
    }
    if (def.op == IrOpcode::ConstInt) {
        // 1-2 ImmV (lo, hi) untuk nilai 64-bit (DEVIATIONS D-4).
        std::uint64_t value = 0;
        if (!decodeConstInt(def, value)) return false;
        out.k = Fact::K::Int;
        out.bits64 = value;
        return true;
    }
    if (def.op == IrOpcode::ConstBool && def.operands.size() == 1) {
        out.k = Fact::K::Bool;
        out.bits = def.operands[0].id != 0 ? 1u : 0u;
        return true;
    }
    if (def.op == IrOpcode::ConstFloat && def.operands.size() == 2) {
        out.k = Fact::K::Float;
        out.bits64 =
            static_cast<std::uint64_t>(def.operands[0].id) |
            (static_cast<std::uint64_t>(def.operands[1].id) << 32);
        out.type = def.resultType;
        return true;
    }
    return false;
}

bool sameFact(const Fact& a, const Fact& b) {
    if (a.k != b.k) return false;
    if (a.k == Fact::K::Float) {
        return a.bits64 == b.bits64 && a.type == b.type;
    }
    if (a.k == Fact::K::Int) {
        return a.bits64 == b.bits64;
    }
    return a.bits == b.bits;
}

// Cari (atau buat) entri FloatBits di constant pool module; kembalikan
// ConstId. Dedup aktif: entri identik (tipe + bit pattern) berbagi id.
ConstId internFloatConst(IrModule& module, IrEditor& editor,
                         TypeId type, std::uint64_t bits) {
    for (ConstId id = 0; id < module.constants.size(); ++id) {
        const IrConstant& c = module.constants[id];
        if (c.kind == IrConstantKind::FloatBits && c.type == type &&
            c.bitPattern == bits) {
            return id;
        }
    }
    IrConstant c;
    c.kind = IrConstantKind::FloatBits;
    c.type = type;
    c.bitPattern = bits;
    ConstId id = InvalidConst;
    if (!editor.appendConst(c, id).ok) return InvalidConst;
    return id;
}

} // namespace

IrPassResult ConstantPropagation::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];

        // 1) Fakta konstanta per SSA value, dengan fixpoint untuk phi
        //    (rantai phi lintas block).
        std::vector<std::optional<Fact>> facts(fn.values.size());
        bool changedRound = true;
        while (changedRound) {
            changedRound = false;
            for (std::uint32_t v = 0; v < fn.values.size(); ++v) {
                if (facts[v].has_value()) continue;
                const IrValue& val = fn.values[v];
                if (val.kind != IrValue::Kind::Instr ||
                    val.payload >= fn.instrs.size()) {
                    continue;
                }
                const IrInstr& def = fn.instrs[val.payload];

                Fact fact;
                if (factFromDef(def, fact)) {
                    facts[v] = fact;
                    changedRound = true;
                    continue;
                }
                // Phi runtuh: seluruh operand bernilai konstan sama
                // (operand ValueV dengan fakta, atau ImmV langsung).
                if (def.op != IrOpcode::Phi || def.operands.empty()) continue;
                std::optional<Fact> acc;
                bool allConst = true;
                for (const IrOperand& op : def.operands) {
                    std::optional<Fact> cur;
                    if (op.kind == IrOperandKind::ImmV) {
                        cur = Fact{};
                        cur->k = Fact::K::Int;
                        cur->bits64 = op.id;
                    } else if (op.kind == IrOperandKind::ValueV &&
                               op.id < facts.size() &&
                               facts[op.id].has_value()) {
                        cur = facts[op.id];
                    }
                    if (!cur.has_value()) { allConst = false; break; }
                    if (!acc.has_value()) acc = cur;
                    else if (!sameFact(*acc, *cur)) { allConst = false; break; }
                }
                if (allConst && acc.has_value()) {
                    facts[v] = acc;
                    changedRound = true;
                }
            }
        }

        // 2) Rewrite operand USE. Instruksi def konstan sendiri tidak
        //    disentuh; Phi tidak ditulis ulang (incoming edge harus tetap
        //    bernilai SSA) — hasil propagasi phi terwujud lewat fakta
        //    value phi: USE-nya yang collapse.
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& blk = fn.blocks[b];
            for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                const InstrId iid = blk.insts[pos];
                if (iid == InvalidInstr || iid >= fn.instrs.size()) continue;
                const IrInstr& instr = fn.instrs[iid];
                if (instr.op == IrOpcode::Phi) continue;
                for (std::size_t oi = 0; oi < instr.operands.size(); ++oi) {
                    const IrOperand& op = instr.operands[oi];
                    if (op.kind != IrOperandKind::ValueV ||
                        op.id >= facts.size() || !facts[op.id].has_value()) {
                        continue;
                    }
                    const Fact& fact = *facts[op.id];
                    IrOperand rewritten;
                    if (fact.k == Fact::K::Float) {
                        const ConstId cid =
                            internFloatConst(module, editor, fact.type,
                                             fact.bits64);
                        if (cid == InvalidConst) {
                            result.ok = false;
                            return result;
                        }
                        rewritten = IrOperand::constant(cid);
                    } else if (fact.k == Fact::K::Bool) {
                        rewritten = IrOperand::imm(fact.bits);
                    } else if (fact.k == Fact::K::Int &&
                               fact.bits64 <= 0xFFFFFFFFull) {
                        rewritten = IrOperand::imm(
                            static_cast<std::uint32_t>(fact.bits64));
                    } else {
                        // Int 64-bit tidak bisa jadi ImmV (D-4) — use
                        // dibiarkan, def ConstInt-nya tetap menjadi
                        // sumber nilai (sound, konservatif).
                        continue;
                    }
                    if (!editor.replaceOperandAt(f, b, pos, oi,
                                                 rewritten).ok) {
                        result.ok = false;
                        return result;
                    }
                    result.changed = true;
                    result.folded += 1;
                }
            }
        }
    }
    return result;
}

} // namespace rhc
