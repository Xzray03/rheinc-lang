// X86_64SysVEmitter.cpp - Implementasi emitter (Fase 2).
//
// Model codegen: NAIVE stack-slot (Q11) — tiap SSA value berupa slot
// 8-byte di frame; register hanya scratch sementara. Struktur siap
// diganti RegAlloc (NaiveRegAlloc/LinearScan) tanpa mengubah API.
//
// Cakupan Fase 2:
//   - Konstanta: ConstInt/ConstBool/ConstString
//   - Aritmetika/bitwise: Add Sub Mul Div DivFloor Mod, BitAnd BitOr
//     BitXor Shl Shr, Neg BitNot Inc Dec
//   - Logika: Cmp* (setcc), Not
//   - Kontrol alur: Br CondBr SwitchInt (cmp chain), Phi (edge moves)
//   - Cast int->int (movsx/movzx); Call builtin (arg int ValueV/ImmV
//     + string); Ret/RetVoid
// Di luar cakupan -> UnsupportedOpcode/Operand (fail-fast).

#include "RhBackend/X86_64SysVEmitter.h"

#include "RhBackend/AsmBuilder.h"
#include "RhBackend/RuntimeContract.h"

#include "RhIR/IrConstant.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rhc {

namespace {

void fail(std::vector<RhBackendError>& errors, RhBackendErrorCode code,
          const std::string& fnName, const std::string& msg) {
    RhBackendError err;
    err.code = code;
    err.functionName = fnName;
    err.message = msg;
    errors.push_back(std::move(err));
}

struct PendingString {
    std::string label;
    IrConstant constant;
};

class FnEmitter {
public:
    FnEmitter(const IrModule& module, const IrFunction& fn, AsmBuilder& out,
              std::vector<RhBackendError>& errors)
        : module_(module), fn_(fn), out_(out), errors_(errors) {}

    bool run(std::vector<PendingString>& pendingStrings) {
        pendingStrings_ = &pendingStrings;
        buildIndices();

        out_.globl(fn_.name);
        out_.typeFunction(fn_.name);
        out_.label(fn_.name);
        out_.ins("pushq %rbp");
        out_.ins("movq %rsp, %rbp");
        if (frameSize_ > 0) {
            out_.ins("subq $" + std::to_string(frameSize_) + ", %rsp");
        }

        for (std::uint32_t b = 0; b < fn_.blocks.size(); ++b) {
            if (!emitBlock(b)) {
                return false;
            }
        }
        return true;
    }

private:
    // ---- indexing: instr/value/block ordinal -> slot/label ----
    void buildIndices() {
        for (ValueId i = 0; i < fn_.values.size(); ++i) {
            const IrValue& v = fn_.values[i];
            if (v.kind != IrValue::Kind::Instr || v.payload >= fn_.instrs.size()) {
                continue;
            }
            valueOfInstr_.emplace(v.payload, i);
            // Slot 8-byte per SSA value (naive).
            const std::int32_t off = -8 * (static_cast<std::int32_t>(slotCount_) + 1);
            slotOfValue_.emplace(i, off);
            ++slotCount_;
        }
        // BlockId = indeks vektor blocks (konvensi IrBuilder).
        frameSize_ = (slotCount_ * 8 + 15) / 16 * 16;
    }

    std::string blockLabel(BlockId b) {
        return ".LB" + std::to_string(b) + "_" + fn_.name;
    }

    // Helper tipe (pola identik PeepholeConstFold).
    bool typeSignedAt(TypeId t) const {
        return t != InvalidType && t < module_.types.size() &&
               module_.types[t].isSigned;
    }
    std::uint32_t typeSizeAt(TypeId t) const {
        return (t != InvalidType && t < module_.types.size())
                   ? module_.types[t].sizeBytes
                   : 0;
    }

    // Operand -> register. ValueV/ImmV lalu dipakai binop/cmp/call/ret.
    bool loadToReg(const IrOperand& op, const std::string& reg) {
        if (op.kind == IrOperandKind::ImmV) {
            out_.ins("movq $" + std::to_string(op.id) + ", %" + reg);
            return true;
        }
        if (op.kind == IrOperandKind::ValueV) {
            const auto it = slotOfValue_.find(op.id);
            if (it == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "operand ValueV tanpa slot");
                return false;
            }
            out_.ins("movq " + std::to_string(it->second) + "(%rbp), %" + reg);
            return true;
        }
        fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
             "operand kind tidak bisa dimuat ke register: " +
                 std::to_string(static_cast<int>(op.kind)));
        return false;
    }

    bool storeReg(const std::string& reg, ValueId vid) {
        const auto it = slotOfValue_.find(vid);
        if (it == slotOfValue_.end()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "hasil tanpa slot");
            return false;
        }
        out_.ins("movq %" + reg + ", " + std::to_string(it->second) + "(%rbp)");
        return true;
    }

    // Konstanta string -> label .rodata unik per ConstId (cache).
    void pushString(const std::string& lbl, const IrConstant& c) {
        for (const PendingString& ps : *pendingStrings_) {
            if (ps.label == lbl) return;
        }
        pendingStrings_->push_back({lbl, c});
    }
    std::string stringLabel(ConstId cid, std::uint32_t instrIdx) {
        const auto it = stringLabels_.find(cid);
        if (it != stringLabels_.end()) return it->second;
        const std::string lbl = ".LC" + std::to_string(instrIdx) + "_" + fn_.name;
        stringLabels_.emplace(cid, lbl);
        return lbl;
    }

    bool resultValue(std::uint32_t instrIdx, ValueId& out) const {
        const auto it = valueOfInstr_.find(instrIdx);
        if (it == valueOfInstr_.end()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "instruksi tanpa value hasil");
            return false;
        }
        out = it->second;
        return true;
    }

    // ---- per-block emission ----
    bool emitBlock(std::uint32_t b) {
        const IrBlock& block = fn_.blocks[b];
        out_.label(blockLabel(static_cast<BlockId>(b)));

        for (std::uint32_t k = 0; k < block.insts.size(); ++k) {
            const InstrId iid = block.insts[k];
            if (iid >= fn_.instrs.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "InstrId invalid");
                return false;
            }
            if (!emitInstr(fn_.instrs[iid], iid, b)) {
                return false;
            }
        }
        return true;
    }

    // Phi moves untuk edge (pred ordinal -> succ block), dipanggil tepat
    // SEBELUM jump terminator di blok pred.
    bool emitPhiMoves(std::uint32_t predOrdinal, BlockId succId) {
        if (succId >= fn_.blocks.size()) return true;
        const IrBlock& succ = fn_.blocks[succId];
        for (const InstrId iid : succ.insts) {
            if (iid >= fn_.instrs.size() || fn_.instrs[iid].op != IrOpcode::Phi) {
                continue;
            }
            const IrInstr& phi = fn_.instrs[iid];
            ValueId phiValue = 0;
            if (!resultValue(iid, phiValue)) return false;
            // Pasangan (BlockV pred, ValueV incoming).
            for (std::size_t k = 0; k + 1 < phi.operands.size(); k += 2) {
                if (phi.operands[k].id != predOrdinal) {
                    continue;
                }
                const IrOperand& incoming = phi.operands[k + 1];
                const auto islot = slotOfValue_.find(incoming.id);
                if (incoming.kind != IrOperandKind::ValueV ||
                    islot == slotOfValue_.end()) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "phi: incoming bukan ValueV berslot");
                    return false;
                }
                const auto pslot = slotOfValue_.find(phiValue);
                out_.ins("movq " + std::to_string(islot->second) + "(%rbp), %rax");
                out_.ins("movq %rax, " + std::to_string(pslot->second) + "(%rbp)");
            }
        }
        return true;
    }

    bool emitInstr(const IrInstr& inst, std::uint32_t instrIdx,
                   std::uint32_t blockOrdinal) {
        ValueId result = 0;
        const bool hasResult = valueOfInstr_.count(instrIdx) != 0;

        switch (inst.op) {
        case IrOpcode::Phi:
            return true;  // nilai diisi oleh edge moves
        case IrOpcode::ConstInt: {
            std::uint64_t value = 0;
            if (!decodeConstInt(inst, value)) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "const_int: operand ImmV invalid");
                return false;
            }
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("movq $" + std::to_string(value) + ", %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::ConstBool: {
            if (inst.operands.empty() || inst.operands[0].kind != IrOperandKind::ImmV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "const_bool: operand ImmV invalid");
                return false;
            }
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("movq $" + std::to_string(inst.operands[0].id) + ", %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::ConstString: {
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::ConstantV ||
                inst.operands[0].id >= module_.constants.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "const_string: operand ConstantV invalid");
                return false;
            }
            const IrConstant& c = module_.constants[inst.operands[0].id];
            const std::string lbl = stringLabel(inst.operands[0].id, instrIdx);
            pushString(lbl, c);
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("leaq " + lbl + "(%rip), %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::Inc:
        case IrOpcode::Dec:
        case IrOpcode::Neg:
        case IrOpcode::BitNot: {
            if (inst.operands.empty() || !loadToReg(inst.operands[0], "rax")) {
                return false;
            }
            if (inst.op == IrOpcode::Inc) out_.ins("incq %rax");
            else if (inst.op == IrOpcode::Dec) out_.ins("decq %rax");
            else if (inst.op == IrOpcode::Neg) out_.ins("negq %rax");
            else out_.ins("notq %rax");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::Not: {
            if (inst.operands.empty() || !loadToReg(inst.operands[0], "rax")) {
                return false;
            }
            out_.ins("testq %rax, %rax");
            out_.ins("sete %al");
            out_.ins("movzbq %al, %rax");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul:
        case IrOpcode::BitAnd: case IrOpcode::BitOr: case IrOpcode::BitXor:
        case IrOpcode::Div: case IrOpcode::DivFloor: case IrOpcode::Mod:
        case IrOpcode::Shl: case IrOpcode::Shr:
            return emitBinop(inst, instrIdx);
        case IrOpcode::CmpEq: case IrOpcode::CmpNe:
        case IrOpcode::CmpLt: case IrOpcode::CmpLe:
        case IrOpcode::CmpGt: case IrOpcode::CmpGe:
            return emitCmp(inst, instrIdx);
        case IrOpcode::Cast:
            return emitCast(inst, instrIdx);
        case IrOpcode::Call:
            return emitCall(inst, instrIdx);
        case IrOpcode::Br: {
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::BlockV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "br: target BlockV invalid");
                return false;
            }
            if (inst.operands[0].id >= fn_.blocks.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "br: block target tak dikenal");
                return false;
            }
            if (!emitPhiMoves(blockOrdinal, inst.operands[0].id)) return false;
            out_.ins("jmp " + blockLabel(inst.operands[0].id));
            return true;
        }
        case IrOpcode::CondBr: {
            if (inst.operands.size() < 3) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "cond_br: operand kurang");
                return false;
            }
            if (!loadToReg(inst.operands[0], "rax")) return false;
            if (inst.operands[1].id >= fn_.blocks.size() ||
                inst.operands[2].id >= fn_.blocks.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "cond_br: block target tak dikenal");
                return false;
            }
            const std::string elseLocal = ".LCBr" + std::to_string(instrIdx) + "_" + fn_.name;
            out_.ins("testq %rax, %rax");
            out_.ins("je " + elseLocal);
            if (!emitPhiMoves(blockOrdinal, inst.operands[1].id)) return false;
            out_.ins("jmp " + blockLabel(inst.operands[1].id));
            out_.label(elseLocal);
            if (!emitPhiMoves(blockOrdinal, inst.operands[2].id)) return false;
            out_.ins("jmp " + blockLabel(inst.operands[2].id));
            return true;
        }
        case IrOpcode::SwitchInt:
            return emitSwitchInt(inst, instrIdx, blockOrdinal);
        case IrOpcode::RetVoid:
            out_.ins("leave");
            out_.ins("retq");
            return true;
        case IrOpcode::Ret: {
            if (!inst.operands.empty() && !loadToReg(inst.operands[0], "rax")) {
                return false;
            }
            out_.ins("leave");
            out_.ins("retq");
            return true;
        }
        default:
            fail(errors_, RhBackendErrorCode::UnsupportedOpcode, fn_.name,
                 "opcode belum didukung Fase 2: " +
                     std::to_string(static_cast<int>(inst.op)));
            return false;
        }
        (void)hasResult;
    }

    bool emitBinop(const IrInstr& inst, std::uint32_t instrIdx) {
        if (inst.operands.size() < 2 ||
            inst.operands[0].kind != IrOperandKind::ValueV ||
            inst.operands[1].kind != IrOperandKind::ValueV) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "binop: operand bukan ValueV");
            return false;
        }
        if (!loadToReg(inst.operands[0], "rax")) return false;
        if (inst.op == IrOpcode::Shl || inst.op == IrOpcode::Shr) {
            if (!loadToReg(inst.operands[1], "rcx")) return false;
            const bool sgn = inst.resultType < module_.types.size() &&
                             typeSignedAt(inst.resultType);
            out_.ins(inst.op == IrOpcode::Shl ? "shlq %cl, %rax"
                                              : (sgn ? "sarq %cl, %rax"
                                                     : "shrq %cl, %rax"));
        } else if (inst.op == IrOpcode::Div || inst.op == IrOpcode::DivFloor ||
                   inst.op == IrOpcode::Mod) {
            if (!loadToReg(inst.operands[1], "rcx")) return false;
            out_.ins("cqo");
            out_.ins("idivq %rcx");  // rax = quo, rdx = rem
            if (inst.op != IrOpcode::Div) {
                // Koreksi floor division (DEVIATIONS D-1): bila sisa != 0
                // dan tanda pembagi berbeda, sesuaikan hasil.
                const std::string done = ".LFlr" + std::to_string(instrIdx) + "_" + fn_.name;
                out_.ins("testq %rdx, %rdx");
                out_.ins("je " + done);
                out_.ins("movq %rax, %rsi");
                out_.ins("xorq %rcx, %rsi");   // tanda quo vs divisor
                out_.ins("jns " + done);
                if (inst.op == IrOpcode::DivFloor) {
                    out_.ins("decq %rax");
                } else {
                    out_.ins("addq %rcx, %rdx");  // floor mod
                }
                out_.label(done);
            }
            if (inst.op == IrOpcode::Mod) {
                out_.ins("movq %rdx, %rax");
            }
        } else {
            if (!loadToReg(inst.operands[1], "rcx")) return false;
            switch (inst.op) {
            case IrOpcode::Add:     out_.ins("addq %rcx, %rax"); break;
            case IrOpcode::Sub:     out_.ins("subq %rcx, %rax"); break;
            case IrOpcode::Mul:     out_.ins("imulq %rcx, %rax"); break;
            case IrOpcode::BitAnd:  out_.ins("andq %rcx, %rax"); break;
            case IrOpcode::BitOr:   out_.ins("orq %rcx, %rax"); break;
            case IrOpcode::BitXor:  out_.ins("xorq %rcx, %rax"); break;
            default: break;
            }
        }
        ValueId result = 0;
        if (!resultValue(instrIdx, result)) return false;
        return storeReg("rax", result);
    }

    bool emitCmp(const IrInstr& inst, std::uint32_t instrIdx) {
        if (inst.operands.size() < 2 ||
            !loadToReg(inst.operands[0], "rax") ||
            !loadToReg(inst.operands[1], "rcx")) {
            return false;
        }
        out_.ins("cmpq %rcx, %rax");
        const char* cc = "e";
        switch (inst.op) {
        case IrOpcode::CmpEq: cc = "e";  break;
        case IrOpcode::CmpNe: cc = "ne"; break;
        case IrOpcode::CmpLt: cc = "l";  break;
        case IrOpcode::CmpLe: cc = "le"; break;
        case IrOpcode::CmpGt: cc = "g";  break;
        case IrOpcode::CmpGe: cc = "ge"; break;
        default: break;
        }
        out_.ins(std::string("set") + cc + " %al");
        out_.ins("movzbq %al, %rax");
        ValueId result = 0;
        if (!resultValue(instrIdx, result)) return false;
        return storeReg("rax", result);
    }

    bool emitCast(const IrInstr& inst, std::uint32_t instrIdx) {
        if (inst.operands.empty() ||
            inst.operands[0].kind != IrOperandKind::ValueV) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "cast: operand bukan ValueV");
            return false;
        }
        const IrValue& srcValue = fn_.values[inst.operands[0].id];
        const IrType& from = module_.types[srcValue.type];
        const IrType& to = module_.types[inst.resultType];
        if (from.kind != IrTypeKind::Int || to.kind != IrTypeKind::Int) {
            fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                 "cast: Fase 2 hanya int->int");
            return false;
        }
        const auto islot = slotOfValue_.find(inst.operands[0].id);
        if (islot == slotOfValue_.end()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "cast: operand tanpa slot");
            return false;
        }
        const std::uint32_t fromB = from.sizeBytes, toB = to.sizeBytes;
        const bool same = fromB == toB;
        const bool sign = typeSignedAt(inst.resultType);
        const std::string src = std::to_string(islot->second) + "(%rbp)";
        if (same) {
            out_.ins("movq " + src + ", %rax");
        } else if (fromB < toB) {
            // widening: movzx (unsigned) / movsx (signed)
            const char* op = sign ? "movsbq" : "movzbq";
            if (fromB == 2) out_.ins(std::string(sign ? "movswq " : "movzwq ") + src + ", %rax");
            else if (fromB == 4) out_.ins(std::string(sign ? "movslq " : "movl ") + src + ", %eax" + (sign ? "" : ""));
            else out_.ins(std::string(op) + " " + src + ", %rax");
        } else {
            // narrowing: muat lalu mask lebar tujuan
            out_.ins("movq " + src + ", %rax");
        }
        ValueId result = 0;
        if (!resultValue(instrIdx, result)) return false;
        return storeReg("rax", result);
    }

    bool emitCall(const IrInstr& inst, std::uint32_t instrIdx) {
        if (inst.operands.empty() ||
            inst.operands[0].kind != IrOperandKind::FuncV ||
            inst.operands[0].id >= module_.functions.size()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "call: target FuncV invalid");
            return false;
        }
        const IrFunction& target = module_.functions[inst.operands[0].id];
        if (target.kind != IrFunctionKind::External || !target.isBuiltin) {
            fail(errors_, RhBackendErrorCode::UnsupportedOpcode, fn_.name,
                 "call: Fase 2 hanya mendukung call ke builtin runtime");
            return false;
        }
        const std::optional<std::string> sym = runtimeSymbolFor(target.name);
        if (!sym) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "call: builtin tanpa simbol runtime: " + target.name);
            return false;
        }

        static const char* kGp[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        std::uint32_t reg = 0;
        for (std::size_t i = 1; i < inst.operands.size(); ++i) {
            const IrOperand& arg = inst.operands[i];
            ConstId cid = InvalidConst;
            if (arg.kind == IrOperandKind::ConstantV) {
                cid = arg.id;
            } else if (arg.kind == IrOperandKind::ValueV &&
                       arg.id < fn_.values.size()) {
                const IrValue& v = fn_.values[arg.id];
                if (v.kind == IrValue::Kind::Instr && v.payload < fn_.instrs.size()) {
                    const IrInstr& def = fn_.instrs[v.payload];
                    if (def.op == IrOpcode::ConstString && !def.operands.empty() &&
                        def.operands[0].kind == IrOperandKind::ConstantV) {
                        cid = def.operands[0].id;
                    }
                }
            }
            const bool isString = cid != InvalidConst &&
                                  cid < module_.constants.size() &&
                                  module_.constants[cid].kind == IrConstantKind::String;
            const std::uint32_t need = isString ? 2 : 1;
            if (reg + need > 6) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "call: melebihi 6 register gp Fase 2");
                return false;
            }
            if (isString) {
                const IrConstant& c = module_.constants[cid];
                const std::string lbl = stringLabel(cid, instrIdx);
                pushString(lbl, c);
                out_.ins(std::string("leaq ") + lbl + "(%rip), %" + kGp[reg]);
                out_.ins(std::string("movq $") + std::to_string(c.bytes.size()) +
                         ", %" + kGp[reg + 1]);
                reg += 2;
            } else if (arg.kind == IrOperandKind::ImmV) {
                out_.ins(std::string("movq $") + std::to_string(arg.id) +
                         ", %" + kGp[reg]);
                ++reg;
            } else if (arg.kind == IrOperandKind::ValueV) {
                const auto s = slotOfValue_.find(arg.id);
                if (s == slotOfValue_.end()) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "call: arg ValueV tanpa slot");
                    return false;
                }
                out_.ins("movq " + std::to_string(s->second) + "(%rbp), %" +
                         kGp[reg]);
                ++reg;
            } else {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "call: bentuk argumen tak didukung");
                return false;
            }
        }
        out_.ins("xorl %eax, %eax");  // variadic System V: al = jml reg xmm
        out_.ins("call " + *sym);
        // Hasil call (bila ada) -> slot.
        const auto rv = valueOfInstr_.find(instrIdx);
        if (rv != valueOfInstr_.end()) {
            return storeReg("rax", rv->second);
        }
        return true;
    }

    bool emitSwitchInt(const IrInstr& inst, std::uint32_t instrIdx,
                       std::uint32_t blockOrdinal) {
        if (inst.operands.empty() || !loadToReg(inst.operands[0], "rax")) {
            return false;
        }
        // Pasangan (nilai, BlockV) lalu BlockV default di operand terakhir.
        const std::size_t n = inst.operands.size();
        if (n < 2) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "switch_int: operand kurang");
            return false;
        }
        // Nilai kasus: ImmV / ConstantV(const_int) / ValueV(const_int SSA).
        for (std::size_t k = 1; k + 1 < n; k += 2) {
            const IrOperand& valOp = inst.operands[k];
            const IrOperand& blkOp = inst.operands[k + 1];
            std::uint64_t caseValue = 0;
            if (valOp.kind == IrOperandKind::ImmV) {
                caseValue = valOp.id;
            } else if (valOp.kind == IrOperandKind::ValueV &&
                       valOp.id < fn_.values.size()) {
                const IrValue& v = fn_.values[valOp.id];
                if (v.kind != IrValue::Kind::Instr || v.payload >= fn_.instrs.size() ||
                    !decodeConstInt(fn_.instrs[v.payload], caseValue)) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "switch_int: nilai kasus bukan konstanta");
                    return false;
                }
            } else {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "switch_int: nilai kasus tak dikenal");
                return false;
            }
            if (blkOp.id >= fn_.blocks.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "switch_int: block tak dikenal");
                return false;
            }
            out_.ins("movq $" + std::to_string(caseValue) + ", %rcx");
            out_.ins("cmpq %rcx, %rax");
            const std::string next = ".LSw" + std::to_string(instrIdx) + "_" +
                                     std::to_string(k) + "_" + fn_.name;
            out_.ins("jne " + next);
            if (!emitPhiMoves(blockOrdinal, blkOp.id)) return false;
            out_.ins("jmp " + blockLabel(blkOp.id));
            out_.label(next);
        }
        if (inst.operands[n - 1].id >= fn_.blocks.size()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "switch_int: default tak dikenal");
            return false;
        }
        if (!emitPhiMoves(blockOrdinal, inst.operands[n - 1].id)) return false;
        out_.ins("jmp " + blockLabel(inst.operands[n - 1].id));
        return true;
    }

    const IrModule& module_;
    const IrFunction& fn_;
    AsmBuilder& out_;
    std::vector<RhBackendError>& errors_;
    std::vector<PendingString>* pendingStrings_ = nullptr;

    std::unordered_map<std::uint32_t, ValueId> valueOfInstr_;
    std::unordered_map<ValueId, std::int32_t> slotOfValue_;
    std::unordered_map<ConstId, std::string> stringLabels_;
    std::uint32_t slotCount_ = 0;
    std::int32_t frameSize_ = 0;
};

}  // namespace

bool X86_64SysVEmitter::emitModule(const FinalIr& finalIr, AsmBuilder& out,
                                   std::vector<RhBackendError>& errors) {
    const IrModule& module = finalIr.module();
    out.comment("Rheinc RhBackend - x86_64-linux-sysv (naive stack-slot)");
    out.text();

    std::vector<PendingString> pendingStrings;
    for (const IrFunction& fn : module.functions) {
        if (fn.kind == IrFunctionKind::External) {
            continue;  // stub: simbol didefinisikan di luar (runtime)
        }
        FnEmitter fe(module, fn, out, errors);
        if (!fe.run(pendingStrings)) {
            return false;
        }
    }

    out.rodata();
    for (const PendingString& ps : pendingStrings) {
        out.align(1);
        out.label(ps.label);
        out.asciiZ(
            std::string(ps.constant.bytes.begin(), ps.constant.bytes.end()));
    }

    out.gnuStackNote();
    return true;
}

}  // namespace rhc
