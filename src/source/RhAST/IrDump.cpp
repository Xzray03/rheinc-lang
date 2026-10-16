#include "RhAST/IrDump.h"

#include "RhAST/IrInstr.h"
#include "RhAST/IrModule.h"
#include "RhAST/IrVersion.h"

#include <cctype>
#include <ostream>
#include <string>
#include <vector>

namespace rhc {

namespace {

// Nama tampil value: nama penuh yang tersimpan ("x.3"); fallback
// penomoran internal untuk value anonim (spesifikasi Bagian 9.2).
std::string valueName(const IrFunction& fn, ValueId id) {
    if (id >= fn.values.size()) return "%v?";
    const std::string& name = fn.values[id].name;
    return name.empty() ? ("%v" + std::to_string(id)) : ("%" + name);
}

std::string opcodeName(IrOpcode op) {
    switch (op) {
    case IrOpcode::ConstInt:    return "const_int";
    case IrOpcode::ConstFloat:  return "const_float";
    case IrOpcode::ConstBool:   return "const_bool";
    case IrOpcode::ConstString: return "const_string";
    case IrOpcode::ConstNull:   return "const_null";
    case IrOpcode::Alloc:       return "alloc";
    case IrOpcode::Load:        return "load";
    case IrOpcode::Store:       return "store";
    case IrOpcode::CopyConst:   return "copy_const";
    case IrOpcode::AddrOf:      return "addr_of";
    case IrOpcode::Deref:       return "deref";
    case IrOpcode::FieldAddr:   return "field_addr";
    case IrOpcode::Construct:   return "construct";
    case IrOpcode::Extract:     return "extract";
    case IrOpcode::Insert:      return "insert";
    case IrOpcode::Add:         return "add";
    case IrOpcode::Sub:         return "sub";
    case IrOpcode::Mul:         return "mul";
    case IrOpcode::Div:         return "div";
    case IrOpcode::Mod:         return "mod";
    case IrOpcode::Pow:         return "pow";
    case IrOpcode::DivFloor:    return "div_floor";
    case IrOpcode::BitAnd:      return "bit_and";
    case IrOpcode::BitOr:       return "bit_or";
    case IrOpcode::BitXor:      return "bit_xor";
    case IrOpcode::Shl:         return "shl";
    case IrOpcode::Shr:         return "shr";
    case IrOpcode::Neg:         return "neg";
    case IrOpcode::BitNot:      return "bit_not";
    case IrOpcode::Inc:         return "inc";
    case IrOpcode::Dec:         return "dec";
    case IrOpcode::And:         return "and";
    case IrOpcode::Or:          return "or";
    case IrOpcode::Not:         return "not";
    case IrOpcode::CmpEq:       return "cmp_eq";
    case IrOpcode::CmpNe:       return "cmp_ne";
    case IrOpcode::CmpLt:       return "cmp_lt";
    case IrOpcode::CmpLe:       return "cmp_le";
    case IrOpcode::CmpGt:       return "cmp_gt";
    case IrOpcode::CmpGe:       return "cmp_ge";
    case IrOpcode::Cast:        return "cast";
    case IrOpcode::Call:        return "call";
    case IrOpcode::CallIndirect:return "call_indirect";
    case IrOpcode::Phi:         return "phi";
    case IrOpcode::MatchTag:    return "match_tag";
    case IrOpcode::Br:          return "br";
    case IrOpcode::CondBr:      return "cond_br";
    case IrOpcode::SwitchTag:   return "switch_tag";
    case IrOpcode::SwitchInt:   return "switch_int";
    case IrOpcode::Ret:         return "ret";
    case IrOpcode::RetVoid:     return "ret_void";
    case IrOpcode::Unreachable: return "unreachable";
    case IrOpcode::Dead:        return "dead";
    }
    return "?";
}

// String aman untuk kutipan: escape \ " dan kontrol dasar.
std::string quote(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 2);
    for (char ch : raw) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                out += "\\x" + std::to_string(
                    static_cast<unsigned int>(static_cast<unsigned char>(ch)));
            } else {
                out += ch;
            }
        }
    }
    return out;
}

class IrDumper {
public:
    IrDumper(std::ostream& out, const IrModule& module,
             const IrDumpOptions& options)
        : out_(out), module_(module), options_(options) {}

    void run() {
        out_ << "(module \"" << quote(module_.sourcePath)
             << "\" ir-version=" << module_.irVersion;
        if (options_.full) {
            dumpTypes();
            dumpConstants();
        }
        for (std::size_t i = 0; i < module_.functions.size(); ++i) {
            dumpFunction(static_cast<FuncId>(i));
        }
        out_ << ")\n";
    }

private:
    std::ostream& out_;
    const IrModule& module_;
    IrDumpOptions options_;
    int indent_ = 1;

    void pad() { out_ << '\n' << std::string(indent_ * 2, ' '); }

    std::string typeRef(TypeId id) const {
        return "@T" + std::to_string(id);
    }

    std::string blockLabel(const IrFunction& fn, BlockId id) const {
        if (id >= fn.blocks.size()) return "?";
        const std::string& label = fn.blocks[id].label;
        return label.empty() ? ("b" + std::to_string(id)) : label;
    }

    void dumpTypes() {
        pad();
        out_ << "(types";
        ++indent_;
        for (std::size_t i = 0; i < module_.types.size(); ++i) {
            const IrType& t = module_.types[i];
            pad();
            out_ << '(' << typeRef(static_cast<TypeId>(i)) << ' ';
            switch (t.kind) {
            case IrTypeKind::Void:   out_ << "void"; break;
            case IrTypeKind::Bool:   out_ << "bool"; break;
            case IrTypeKind::Int:
                out_ << "int signed=" << (t.isSigned ? 1 : 0)
                     << " width=" << t.sizeBytes;
                break;
            case IrTypeKind::Float:
                out_ << "float width=" << t.sizeBytes;
                break;
            case IrTypeKind::Pointer:
                out_ << "ptr " << typeRef(t.elementType);
                break;
            case IrTypeKind::Array:
                out_ << "array size=" << t.arraySize << ' '
                     << typeRef(t.elementType);
                break;
            case IrTypeKind::View:
                out_ << "view " << typeRef(t.elementType);
                break;
            case IrTypeKind::Struct:
                out_ << "struct \"" << quote(t.name) << "\" fields=";
                ++indent_;
                for (const IrField& field : t.fields) {
                    pad();
                    out_ << "(\"" << quote(field.name) << "\" "
                         << typeRef(field.type)
                         << " offset=" << field.offsetBytes;
                    if (field.bitWidth) {
                        out_ << " bit=" << static_cast<int>(field.bitWidth)
                             << "@" << static_cast<int>(field.bitOffset);
                    }
                    out_ << ')';
                }
                --indent_;
                out_ << ' ';
                break;
            case IrTypeKind::Sum:
                out_ << "sum \"" << quote(t.name) << "\" variants=";
                ++indent_;
                for (const IrVariant& variant : t.variants) {
                    pad();
                    out_ << "(\"" << quote(variant.name)
                         << "\" tag=" << variant.tagValue;
                    if (variant.payloadType != InvalidType) {
                        out_ << ' ' << typeRef(variant.payloadType);
                    }
                    out_ << ')';
                }
                --indent_;
                out_ << ' ';
                break;
            case IrTypeKind::FnPtr:
                out_ << "fnptr (";
                for (std::size_t p = 0; p < t.paramTypes.size(); ++p) {
                    if (p) out_ << ' ';
                    out_ << typeRef(t.paramTypes[p]);
                }
                out_ << ") -> " << typeRef(t.returnType);
                break;
            }
            if (options_.full && t.sizeBytes) {
                out_ << " size=" << t.sizeBytes
                     << " align=" << t.alignBytes;
            }
            out_ << ')';
        }
        --indent_;
        out_ << ')';
    }

    void dumpConstants() {
        pad();
        out_ << "(constants";
        ++indent_;
        for (std::size_t i = 0; i < module_.constants.size(); ++i) {
            const IrConstant& c = module_.constants[i];
            pad();
            out_ << "(@C" << i << ' ';
            switch (c.kind) {
            case IrConstantKind::String: {
                std::string text(c.bytes.begin(), c.bytes.end());
                out_ << "string \"" << quote(text) << "\" len=" << c.length;
                break;
            }
            case IrConstantKind::BigInt:
                out_ << "bigint bytes=" << c.bytes.size();
                break;
            case IrConstantKind::FloatBits:
                out_ << "float-bits 0x" << std::hex << c.bitPattern << std::dec;
                break;
            case IrConstantKind::ArrayBlob:
                out_ << "array-blob bytes=" << c.bytes.size()
                     << " elements=" << c.length;
                break;
            }
            out_ << " type=" << typeRef(c.type) << ')';
        }
        --indent_;
        out_ << ')';
    }

    // Operand: value/konstanta/block/func/type/imm.
    std::string operand(const IrFunction& fn, const IrOperand& op) const {
        switch (op.kind) {
        case IrOperandKind::ValueV:
            return valueName(fn, op.id);
        case IrOperandKind::ConstantV:
            return "@C" + std::to_string(op.id);
        case IrOperandKind::BlockV:
            return blockLabel(fn, op.id);
        case IrOperandKind::FuncV: {
            const FuncId fid = op.id;
            if (fid < module_.functions.size()) {
                return "@" + module_.functions[fid].name;
            }
            return "@f?";
        }
        case IrOperandKind::TypeV:
            return typeRef(op.id);
        case IrOperandKind::ImmV:
            return std::to_string(op.id);
        }
        return "?";
    }

    void dumpInstr(const IrFunction& fn, const IrInstr& inst) {
        pad();
        const std::string opName = opcodeName(inst.op);
        const bool hasResult = inst.resultType != InvalidType;

        // Phi: format pasangan [ block, value ].
        if (inst.op == IrOpcode::Phi) {
            out_ << valueName(fn, valueOf(fn, inst)) << " = phi ";
            for (std::size_t i = 0; i + 1 < inst.operands.size(); i += 2) {
                if (i) out_ << ", ";
                out_ << "[ " << operand(fn, inst.operands[i]) << ", "
                     << operand(fn, inst.operands[i + 1]) << " ]";
            }
            return;
        }

        // Switch/match: subjek + pasangan (nilai, target) + default.
        if (inst.op == IrOpcode::SwitchTag || inst.op == IrOpcode::SwitchInt) {
            out_ << '(' << opName << ' ' << operand(fn, inst.operands[0]);
            for (std::size_t i = 1; i < inst.operands.size(); ++i) {
                out_ << ' ' << operand(fn, inst.operands[i]);
            }
            out_ << ')';
            return;
        }

        // Terminator berbentuk s-expression.
        if (isTerminator(inst.op)) {
            out_ << '(' << opName;
            for (const IrOperand& op : inst.operands) {
                out_ << ' ' << operand(fn, op);
            }
            out_ << ')';
            return;
        }

        if (hasResult) {
            out_ << valueName(fn, valueOf(fn, inst)) << " = ";
        }
        out_ << opName;
        for (const IrOperand& op : inst.operands) {
            out_ << ' ' << operand(fn, op);
        }
    }

    // Instruksi non-terminator menghasilkan value dengan payload InstrId.
    ValueId valueOf(const IrFunction& fn, const IrInstr& inst) const {
        for (std::size_t v = 0; v < fn.values.size(); ++v) {
            const IrValue& value = fn.values[v];
            if (value.kind == IrValue::Kind::Instr &&
                fn.instrs[value.payload].epoch == inst.epoch &&
                &fn.instrs[value.payload] == &inst) {
                return static_cast<ValueId>(v);
            }
        }
        return InvalidValue;
    }

    void dumpFunction(FuncId funcId) {
        const IrFunction& fn = module_.functions[funcId];
        pad();
        out_ << "(function @" << fn.name;
        if (options_.full && fn.fnType != InvalidType) {
            out_ << ' ' << typeRef(fn.fnType);
        }
        ++indent_;
        for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& blk = fn.blocks[b];
            pad();
            out_ << "(block " << blockLabel(fn, static_cast<BlockId>(b));
            if (options_.full && !blk.preds.empty()) {
                out_ << " preds=";
                for (BlockId pred : blk.preds) {
                    out_ << ' ' << blockLabel(fn, pred);
                }
            }
            ++indent_;
            for (InstrId id : blk.insts) {
                if (fn.instrs[id].op == IrOpcode::Dead) continue;
                dumpInstr(fn, fn.instrs[id]);
            }
            --indent_;
            out_ << ')';
        }
        --indent_;
        out_ << ')';
    }
};

} // namespace

// Dump deterministik (spesifikasi Bagian 9.2): fungsi mengikuti urutan
// deklarasi sumber; block/value mengikuti urutan insertion.
void dumpIrModule(std::ostream& out, const IrModule& module,
                  const IrDumpOptions& options) {
    IrDumper dumper(out, module, options);
    dumper.run();
    (void)IrFormatVersion;
}

} // namespace rhc
