#include "RhOptimizer/PeepholeConstFold.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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

// Konstanta ter-resolve: nilai int (bit pattern), float (bit pattern
// 64-bit), atau bool. asal: ImmV langsung, atau ValueV yang didefinisikan
// instruksi ConstInt/ConstFloat/ConstBool pada fungsi yang sama.
struct Resolved {
    enum class K : std::uint8_t { Int, Float, Bool } k = K::Int;
    std::uint64_t bits = 0;   // Int: nilai bit (<=64 bit); Float: IEEE754
    bool b = false;
    TypeId type = InvalidType;
};

double floatBitsToDouble(std::uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}
std::uint64_t doubleToFloatBits(double d) {
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits;
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

                const auto typeKindAt = [&module, &fn](TypeId t) {
                    if (t == InvalidType || t >= module.types.size()) {
                        return IrTypeKind::Void;
                    }
                    return module.types[t].kind;
                };
                const auto typeSignedAt = [&module](TypeId t) -> bool {
                    return t != InvalidType && t < module.types.size() &&
                           module.types[t].isSigned;
                };
                const auto typeSizeAt = [&module](TypeId t) -> std::uint32_t {
                    return (t != InvalidType && t < module.types.size())
                               ? module.types[t].sizeBytes
                               : 0;
                };

                // Resolusi operand -> konstanta terketik.
                auto resolve = [&fn, &module, &typeKindAt, &typeSizeAt](
                                   const IrOperand& op,
                                   Resolved& out) -> bool {
                    if (op.kind == IrOperandKind::ImmV) {
                        out.k = Resolved::K::Int;
                        out.bits = op.id;
                        return true;
                    }
                    // Jalur khusus hasil ConstantPropagation: konstanta
                    // float dipropagasikan sebagai ConstantV (FloatBits
                    // 64-bit di constant pool).
                    if (op.kind == IrOperandKind::ConstantV) {
                        if (op.id >= module.constants.size()) return false;
                        const IrConstant& c = module.constants[op.id];
                        if (c.kind != IrConstantKind::FloatBits) {
                            return false;
                        }
                        out.k = Resolved::K::Float;
                        out.bits = c.bitPattern;
                        out.type = c.type;
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
                    out.type = def.resultType;
                    if (def.op == IrOpcode::ConstInt) {
                        // 1-2 ImmV (lo, hi) untuk nilai 64-bit (D-4).
                        std::uint64_t value = 0;
                        if (!decodeConstInt(def, value)) return false;
                        out.k = Resolved::K::Int;
                        out.bits = value;
                        return true;
                    }
                    if (def.operands.empty() ||
                        def.operands[0].kind != IrOperandKind::ImmV) {
                        return false;
                    }
                    if (def.op == IrOpcode::ConstBool) {
                        out.k = Resolved::K::Bool;
                        out.b = def.operands[0].id != 0;
                        return true;
                    }
                    if (def.op == IrOpcode::ConstFloat) {
                        // desimal = double 64-bit SELALU: 2 ImmV (lo, hi).
                        if (def.operands.size() != 2) return false;
                        out.k = Resolved::K::Float;
                        out.bits =
                            static_cast<std::uint64_t>(def.operands[0].id) |
                            (static_cast<std::uint64_t>(def.operands[1].id)
                             << 32);
                        return true;
                    }
                    return false;
                };

                const IrTypeKind rk = typeKindAt(instr.resultType);

                IrInstr folded = instr;
                bool done = false;
                auto emitInt = [&](std::uint64_t bits) {
                    folded.op = IrOpcode::ConstInt;
                    folded.operands = encodeConstIntOperands(bits);
                    folded.name.clear();
                    done = true;
                };
                auto emitBool = [&](bool v) {
                    folded.op = IrOpcode::ConstBool;
                    folded.operands = {IrOperand::imm(v ? 1u : 0u)};
                    folded.name.clear();
                    done = true;
                };
                auto emitFloat = [&](double d) {
                    folded.op = IrOpcode::ConstFloat;
                    const std::uint64_t bits = doubleToFloatBits(d);
                    folded.operands = {
                        IrOperand::imm(static_cast<std::uint32_t>(bits)),
                        IrOperand::imm(static_cast<std::uint32_t>(bits >> 32))};
                    folded.name.clear();
                    done = true;
                };

                if (instr.op == IrOpcode::Cast &&
                           instr.operands.size() == 2) {
                    // Cast [0](ValueV v) [1](TypeV target): operand [0]
                    // konstan -> konversi statis penuh; [1] tetap ImmV?
                    // IrOperand::type() menghasilkan TypeV; verifikasi.
                    Resolved X;
                    if (instr.operands[1].kind != IrOperandKind::TypeV ||
                        instr.operands[1].id >= module.types.size()) {
                        continue;
                    }
                    if (!resolve(instr.operands[0], X)) continue;
                    const TypeId target = instr.operands[1].id;
                    const IrTypeKind tk = typeKindAt(target);
                    if (X.k == Resolved::K::Int) {
                        // Sign/zero extension 64-bit penuh dari lebar
                        // tipe sumber (X.type), bukan cuma 32-bit.
                        const std::uint32_t srcBytes = typeSizeAt(X.type);
                        const unsigned srcWidth =
                            std::min(64u, (srcBytes == 0 ? 8u : srcBytes) * 8u);
                        const std::uint64_t srcMask =
                            srcWidth >= 64 ? ~0ull : ((1ull << srcWidth) - 1);
                        std::uint64_t raw = X.bits & srcMask;
                        if (typeSignedAt(X.type) && srcWidth < 64 &&
                            (raw & (1ull << (srcWidth - 1)))) {
                            raw |= ~srcMask;
                        }
                        const std::int64_t sv =
                            static_cast<std::int64_t>(raw);
                        if (tk == IrTypeKind::Int) {
                            const std::uint32_t bytes = typeSizeAt(target);
                            std::uint64_t mask =
                                bytes >= 8 ? ~0ull
                                           : ((1ull << (bytes * 8)) - 1);
                            emitInt(static_cast<std::uint64_t>(sv) & mask);
                        } else if (tk == IrTypeKind::Float) {
                            emitFloat(static_cast<double>(sv));
                        } else if (tk == IrTypeKind::Bool) {
                            emitBool(sv != 0);
                        }
                    } else if (X.k == Resolved::K::Float) {
                        const double d = floatBitsToDouble(X.bits);
                        if (tk == IrTypeKind::Int) {
                            const std::int64_t sv =
                                typeSizeAt(target) == 8
                                    ? static_cast<std::int64_t>(d)
                                    : static_cast<std::int64_t>(
                                          static_cast<std::int32_t>(d));
                            emitInt(static_cast<std::uint64_t>(sv));
                        } else if (tk == IrTypeKind::Float) {
                            emitFloat(d);
                        } else if (tk == IrTypeKind::Bool) {
                            emitBool(d != 0.0);
                        }
                    } else if (X.k == Resolved::K::Bool) {
                        if (tk == IrTypeKind::Int) {
                            emitInt(X.b ? 1 : 0);
                        } else if (tk == IrTypeKind::Float) {
                            emitFloat(X.b ? 1.0 : 0.0);
                        } else if (tk == IrTypeKind::Bool) {
                            emitBool(X.b);
                        }
                    }
                }

                if (instr.operands.size() == 2) {
                    Resolved L, R;
                    const bool haveL = resolve(instr.operands[0], L);
                    const bool haveR = resolve(instr.operands[1], R);
                    if (!haveL && !haveR) continue;

                    const bool isCmp =
                        instr.op >= IrOpcode::CmpEq &&
                        instr.op <= IrOpcode::CmpGe;
                    const bool intOp =
                        rk == IrTypeKind::Int ||
                        (isCmp && haveL && L.k == Resolved::K::Int &&
                         haveR && R.k == Resolved::K::Int);
                    const bool floatOp =
                        rk == IrTypeKind::Float ||
                        (isCmp && haveL && L.k == Resolved::K::Float &&
                         haveR && R.k == Resolved::K::Float);

                    if (haveL && haveR && intOp &&
                        L.k == Resolved::K::Int &&
                        R.k == Resolved::K::Int) {
                        // Fold 64-bit penuh; hasil aritmetika dimask ke
                        // lebar resultType. Sign extension mengikuti tipe
                        // masing-masing operand (cmp) / resultType (lainnya).
                        const std::uint64_t lhs = L.bits;
                        const std::uint64_t rhs = R.bits;
                        const auto toSigned =
                            [&typeSizeAt, &typeSignedAt](
                                std::uint64_t value, TypeId type) -> std::int64_t {
                                const std::uint32_t b = typeSizeAt(type);
                                const unsigned w =
                                    std::min(64u, (b == 0 ? 8u : b) * 8u);
                                const std::uint64_t m =
                                    w >= 64 ? ~0ull : ((1ull << w) - 1);
                                value &= m;
                                if (!typeSignedAt(type)) {
                                    return static_cast<std::int64_t>(value);
                                }
                                if (w >= 64) {
                                    return static_cast<std::int64_t>(value);
                                }
                                const std::uint64_t signBit = 1ull << (w - 1);
                                if (value & signBit) value |= ~m;
                                return static_cast<std::int64_t>(value);
                            };
                        if (isCmp) {
                            const std::int64_t sL = toSigned(lhs, L.type);
                            const std::int64_t sR = toSigned(rhs, R.type);
                            bool v = false;
                            switch (instr.op) {
                            case IrOpcode::CmpEq: v = lhs == rhs; break;
                            case IrOpcode::CmpNe: v = lhs != rhs; break;
                            case IrOpcode::CmpLt: v = sL < sR; break;
                            case IrOpcode::CmpLe: v = sL <= sR; break;
                            case IrOpcode::CmpGt: v = sL > sR; break;
                            case IrOpcode::CmpGe: v = sL >= sR; break;
                            default: continue;
                            }
                            emitBool(v);
                        } else {
                            const std::uint32_t bytes =
                                typeSizeAt(instr.resultType);
                            const unsigned width =
                                std::min(64u, (bytes == 0 ? 8u : bytes) * 8u);
                            const std::uint64_t mask =
                                width >= 64 ? ~0ull : ((1ull << width) - 1);
                            const bool sgn = typeSignedAt(instr.resultType);
                            const std::int64_t sL = toSigned(lhs, instr.resultType);
                            const std::int64_t sR = toSigned(rhs, instr.resultType);
                            std::uint64_t v = 0;
                            bool ok = true;
                            switch (instr.op) {
                            case IrOpcode::Add: v = lhs + rhs; break;
                            case IrOpcode::Sub: v = lhs - rhs; break;
                            case IrOpcode::Mul: v = lhs * rhs; break;
                            case IrOpcode::Div:
                            case IrOpcode::DivFloor:
                                if (rhs == 0) { ok = false; break; }
                                v = static_cast<std::uint64_t>(
                                    sgn ? floorDiv(sL, sR) : sL / sR);
                                break;
                            case IrOpcode::Mod:
                                if (rhs == 0) { ok = false; break; }
                                v = static_cast<std::uint64_t>(
                                    sgn ? floorMod(sL, sR) : sL % sR);
                                break;
                            case IrOpcode::Pow: {
                                if (rhs > 10000) { ok = false; break; }
                                std::uint64_t acc = 1;
                                for (std::uint64_t i = 0; i < rhs; ++i) {
                                    acc *= lhs;
                                }
                                v = acc;
                                break;
                            }
                            case IrOpcode::BitAnd: v = lhs & rhs; break;
                            case IrOpcode::BitOr:  v = lhs | rhs; break;
                            case IrOpcode::BitXor: v = lhs ^ rhs; break;
                            case IrOpcode::Shl:
                                if (rhs >= width) { ok = false; break; }
                                v = lhs << rhs;
                                break;
                            case IrOpcode::Shr:
                                if (rhs >= width) { ok = false; break; }
                                v = sgn ? static_cast<std::uint64_t>(sL >> rhs)
                                        : lhs >> rhs;
                                break;
                            default: continue;
                            }
                            if (ok) emitInt(v & mask);
                        }
                    } else if (haveL && haveR && floatOp &&
                               L.k == Resolved::K::Float &&
                               R.k == Resolved::K::Float) {
                        const double a = floatBitsToDouble(L.bits);
                        const double c = floatBitsToDouble(R.bits);
                        if (isCmp) {
                            bool v = false;
                            switch (instr.op) {
                            case IrOpcode::CmpEq: v = a == c; break;
                            case IrOpcode::CmpNe: v = a != c; break;
                            case IrOpcode::CmpLt: v = a < c; break;
                            case IrOpcode::CmpLe: v = a <= c; break;
                            case IrOpcode::CmpGt: v = a > c; break;
                            case IrOpcode::CmpGe: v = a >= c; break;
                            default: continue;
                            }
                            emitBool(v);
                        } else {
                            double v = 0;
                            switch (instr.op) {
                            case IrOpcode::Add: v = a + c; break;
                            case IrOpcode::Sub: v = a - c; break;
                            case IrOpcode::Mul: v = a * c; break;
                            case IrOpcode::Div: v = a / c; break;  // IEEE inf/nan aman
                            case IrOpcode::Mod: v = std::fmod(a, c); break;
                            case IrOpcode::Pow: v = std::pow(a, c); break;
                            case IrOpcode::DivFloor:
                                v = std::floor(a / c);
                                break;
                            default: continue;  // bitwise tak berlaku float
                            }
                            emitFloat(v);
                        }
                    } else if (instr.op == IrOpcode::And &&
                               (haveL || haveR)) {
                        // Aturan satu sisi: salah satu operand pasti
                        // false -> hasil false; keduanya diketahui -> AND.
                        if ((haveL && L.k == Resolved::K::Bool && !L.b) ||
                            (haveR && R.k == Resolved::K::Bool && !R.b)) {
                            emitBool(false);
                        } else if (haveL && haveR &&
                                   L.k == Resolved::K::Bool &&
                                   R.k == Resolved::K::Bool) {
                            emitBool(L.b && R.b);
                        }
                    } else if (instr.op == IrOpcode::Or &&
                               (haveL || haveR)) {
                        if ((haveL && L.k == Resolved::K::Bool && L.b) ||
                            (haveR && R.k == Resolved::K::Bool && R.b)) {
                            emitBool(true);
                        } else if (haveL && haveR &&
                                   L.k == Resolved::K::Bool &&
                                   R.k == Resolved::K::Bool) {
                            emitBool(L.b || R.b);
                        }
                    }
                } else if (instr.operands.size() == 1) {
                    Resolved X;
                    if (!resolve(instr.operands[0], X)) continue;
                    if ((instr.op == IrOpcode::Neg ||
                         instr.op == IrOpcode::BitNot) &&
                        X.k == Resolved::K::Int) {
                        // 64-bit penuh; mask lebar resultType diterapkan
                        // di emitInt oleh pemanggil (v & mask tidak untuk
                        // unary) — maka mask di sini eksplisit.
                        const std::uint32_t bytes =
                            typeSizeAt(instr.resultType);
                        const unsigned width =
                            std::min(64u, (bytes == 0 ? 8u : bytes) * 8u);
                        const std::uint64_t m =
                            width >= 64 ? ~0ull : ((1ull << width) - 1);
                        const std::uint64_t x = X.bits & m;
                        emitInt((instr.op == IrOpcode::Neg ? (0ull - x) : ~x) & m);
                    } else if (instr.op == IrOpcode::Neg &&
                               X.k == Resolved::K::Float) {
                        emitFloat(-floatBitsToDouble(X.bits));
                    } else if (instr.op == IrOpcode::Not &&
                               (X.k == Resolved::K::Bool ||
                                X.k == Resolved::K::Int)) {
                        // Operand ImmV langsung terklasifikasi Int —
                        // nilai 0/1 tetap valid sebagai boolean.
                        const bool truthy =
                            X.k == Resolved::K::Bool ? X.b : X.bits != 0;
                        emitBool(!truthy);
                    }
                }
                if (!done) continue;

                if (!editor.replaceInstr(f, b, pos, folded).ok) {
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
