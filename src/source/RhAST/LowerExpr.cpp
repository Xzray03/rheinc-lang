#include "AstToIrTranslatorImpl.h"

#include "RhLexer/TokenKind.h"
#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/Type.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace rhc {

namespace {

// Operand ImmV hanya muat 32 bit; ConstInt menyimpan 1-2 ImmV
// (lo, hi) untuk integer hingga 64 bit. Lebih besar -> const_encode_failed.
std::vector<IrOperand> intOperands(std::uint64_t value) {
    if (value <= std::numeric_limits<std::uint32_t>::max()) {
        return {IrOperand::imm(static_cast<std::uint32_t>(value))};
    }
    return {IrOperand::imm(static_cast<std::uint32_t>(value & 0xFFFFFFFFu)),
            IrOperand::imm(static_cast<std::uint32_t>(value >> 32))};
}

// Parser tidak membedakan literal desimal: keduanya IntegerLiteralExpr
// dengan teks mentah. Deteksi float lewat '.' / exponent, lalu decode
// ke bit pattern sesuai lebar tipe hasil (f32/f64).
bool looksLikeFloat(const std::string& text) {
    return text.find('.') != std::string::npos ||
           text.find('e') != std::string::npos ||
           text.find('E') != std::string::npos;
}

bool parseFloatText(const std::string& text, std::uint64_t& bitsOut,
                    bool isDouble) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char ch : text) {
        if (ch != '_') cleaned.push_back(ch);
    }
    try {
        std::size_t pos = 0;
        if (isDouble) {
            const double value = std::stod(cleaned, &pos);
            if (pos != cleaned.size()) return false;
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) >= sizeof(value), "");
            std::memcpy(&bits, &value, sizeof(value));
            bitsOut = bits;
        } else {
            const float value = std::stof(cleaned, &pos);
            if (pos != cleaned.size()) return false;
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(value));
            bitsOut = bits;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool parseIntegerText(const std::string& text, std::uint64_t& out) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char ch : text) {
        if (ch != '_') cleaned.push_back(ch);
    }
    if (cleaned.empty()) return false;
    // Literal biner 0b/0B — stoull(base 0) tidak mendukung prefix ini.
    if (cleaned.size() > 2 && cleaned[0] == '0' &&
        (cleaned[1] == 'b' || cleaned[1] == 'B')) {
        std::uint64_t value = 0;
        for (std::size_t i = 2; i < cleaned.size(); ++i) {
            if (cleaned[i] != '0' && cleaned[i] != '1') return false;
            value = (value << 1) | static_cast<std::uint64_t>(cleaned[i] - '0');
        }
        out = value;
        return true;
    }
    try {
        std::size_t pos = 0;
        const unsigned long long value =
            std::stoull(cleaned, &pos, 0);
        if (pos != cleaned.size()) return false;
        out = static_cast<std::uint64_t>(value);
        return true;
    } catch (...) {
        return false;
    }
}

// Decode isi char literal (text termasuk tanda kutip).
bool decodeCharLiteral(const std::string& text, std::uint32_t& out) {
    if (text.size() < 3) return false;
    std::size_t i = 1;  // lewati kutip pembuka
    const std::size_t end = text.size() - 1;  // kutip penutup
    if (i >= end) return false;
    if (text[i] == '\\') {
        if (i + 1 >= end) return false;
        switch (text[i + 1]) {
        case 'n': out = '\n'; return true;
        case 't': out = '\t'; return true;
        case 'r': out = '\r'; return true;
        case '0': out = '\0'; return true;
        case '\\': out = '\\'; return true;
        case '\'': out = '\''; return true;
        case '"': out = '"'; return true;
        default: return false;  // escape tak dikenal -> gagal jujur
        }
    }
    // codepoint UTF-8 pertama (cukup untuk BMP awal; dekoder penuh
    // digabung saat lexer expose decoded value)
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80) {
        out = c;
        return true;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < end) {
        out = ((c & 0x1F) << 6) |
              (static_cast<unsigned char>(text[i + 1]) & 0x3F);
        return true;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < end) {
        out = ((c & 0x0F) << 12) |
              ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
              (static_cast<unsigned char>(text[i + 2]) & 0x3F);
        return true;
    }
    return false;
}

// Decode string literal (text termasuk tanda kutip; raw = tanpa escape).
bool decodeStringLiteral(const StringLiteralExpr& node, std::string& out) {
    if (node.text.size() < 2) return false;
    const std::size_t end = node.text.size() - 1;  // kutip penutup
    std::string decoded;
    decoded.reserve(node.text.size());
    std::size_t i = 1;  // lewati kutip pembuka
    while (i < end) {
        if (node.text[i] == '\\' && !node.raw) {
            if (i + 1 >= end) return false;
            switch (node.text[i + 1]) {
            case 'n': decoded.push_back('\n'); break;
            case 't': decoded.push_back('\t'); break;
            case 'r': decoded.push_back('\r'); break;
            case '0': decoded.push_back('\0'); break;
            case '\\': decoded.push_back('\\'); break;
            case '"': decoded.push_back('"'); break;
            case '\'': decoded.push_back('\''); break;
            default: return false;  // escape tak dikenal -> gagal jujur
            }
            i += 2;
        } else {
            decoded.push_back(node.text[i]);
            ++i;
        }
    }
    out = std::move(decoded);
    return true;
}

// Compound assignment (+= -= *= /= %= **= //= <<= >>= &= |= ^=) ->
// opcode operasi yang sesuai. Dead bila bukan compound-assignment.
IrOpcode compoundOpcode(TokenKind op) {
    switch (op) {
    case TokenKind::PlusEqual:            return IrOpcode::Add;
    case TokenKind::MinusEqual:           return IrOpcode::Sub;
    case TokenKind::StarEqual:            return IrOpcode::Mul;
    case TokenKind::SlashEqual:           return IrOpcode::Div;
    case TokenKind::PercentEqual:         return IrOpcode::Mod;
    case TokenKind::StarStarEqual:        return IrOpcode::Pow;
    case TokenKind::SlashSlashEqual:      return IrOpcode::DivFloor;
    case TokenKind::LessLessEqual:        return IrOpcode::Shl;
    case TokenKind::GreaterGreaterEqual:  return IrOpcode::Shr;
    case TokenKind::AmpEqual:             return IrOpcode::BitAnd;
    case TokenKind::PipeEqual:            return IrOpcode::BitOr;
    case TokenKind::CaretEqual:           return IrOpcode::BitXor;
    default:                              return IrOpcode::Dead;
    }
}

// Cari varian sum bernama `caseName` pada tipe sum IR.
// Mengembalikan indeks varian atau -1.
int findSumVariant(const IrModule& module, TypeId sumType,
                   const std::string& caseName) {
    if (sumType >= module.types.size() ||
        module.types[sumType].kind != IrTypeKind::Sum) {
        return -1;
    }
    const IrType& t = module.types[sumType];
    for (std::size_t i = 0; i < t.variants.size(); ++i) {
        if (t.variants[i].name == caseName) return static_cast<int>(i);
    }
    return -1;
}

IrOpcode binaryOpcode(TokenKind op) {
    switch (op) {
    case TokenKind::Plus:            return IrOpcode::Add;
    case TokenKind::Minus:           return IrOpcode::Sub;
    case TokenKind::Star:            return IrOpcode::Mul;
    case TokenKind::Slash:           return IrOpcode::Div;
    case TokenKind::Percent:         return IrOpcode::Mod;
    case TokenKind::StarStar:        return IrOpcode::Pow;
    case TokenKind::SlashSlash:      return IrOpcode::DivFloor;
    case TokenKind::Amp:             return IrOpcode::BitAnd;
    case TokenKind::Pipe:            return IrOpcode::BitOr;
    case TokenKind::Caret:           return IrOpcode::BitXor;
    case TokenKind::LessLess:        return IrOpcode::Shl;
    case TokenKind::GreaterGreater:  return IrOpcode::Shr;
    case TokenKind::KwDan:           return IrOpcode::And;
    case TokenKind::KwAtau:          return IrOpcode::Or;
    case TokenKind::PlusGreater:     return IrOpcode::Append;
    case TokenKind::EqualEqual:      return IrOpcode::CmpEq;
    case TokenKind::BangEqual:       return IrOpcode::CmpNe;
    case TokenKind::Less:            return IrOpcode::CmpLt;
    case TokenKind::LessEqual:       return IrOpcode::CmpLe;
    case TokenKind::Greater:         return IrOpcode::CmpGt;
    case TokenKind::GreaterEqual:    return IrOpcode::CmpGe;
    default:                         return IrOpcode::Dead;
    }
}

} // namespace

FuncId AstToIrTranslator::Impl::resolveCallee(const ExprNode* callee) {
    if (callee->kind != AstKind::IdentifierExpr) {
        error(AstIrErrorCode::UnsupportedNode, callee,
              "callee panggilan non-identifier (method/value fn) belum "
              "didukung lowering");
        return InvalidFunc;
    }
    const auto* identifier = static_cast<const IdentifierExpr*>(callee);
    const auto found = functionsByName.find(identifier->name);
    if (found != functionsByName.end()) return found->second;

    // Fungsi eksternal/builtin yang tidak dideklarasikan di modul:
    // stub eksternal dibuat otomatis (variadik, tanpa signature IR).
    const FuncId stub = builder.createFunction(identifier->name, InvalidType,
                                               callee->range.begin);
    functionsByName[identifier->name] = stub;
    module.globals[identifier->name] = stub;
    return stub;
}

ValueId AstToIrTranslator::Impl::lowerExpr(const ExprNode* expr) {
    // Wrapper: kontrak InvalidValue HARUS dibarengi error/fatal. Tangkap
    // pelanggar kontrak untuk diagnosis.
    if (failed || builder.broken()) return InvalidValue;
    const ValueId lowered = lowerExprImpl(expr);
    if (lowered == InvalidValue && !failed && !builder.broken()) {
        // Kontrak: InvalidValue HARUS dibarengi error. Jalur yang
        // tersisa adalah tipe hasil yang tak dapat dipetakan (semantic
        // memberi ErrorType tanpa diagnostic, mis. pemakaian fungsi
        // tak dikenal) — laporkan jujur, jangan gagal sunyi.
        error(AstIrErrorCode::TypeMappingFailed, expr,
              "ekspresi tidak menghasilkan nilai IR (tipe hasil tidak "
              "dapat dipetakan — kemungkinan pemakaian fungsi/tipe tak "
              "dikenal yang dibiarkan semantic)");
    }
    return lowered;
}

ValueId AstToIrTranslator::Impl::lowerExprImpl(const ExprNode* expr) {
    if (failed || builder.broken()) return InvalidValue;
    const Type* exprType = semantic->context().getExprType(expr);
    // ErrorType (permukaan MAUPUN bersarang, mis. PointerType{Error})
    // ditunda pemetaannya: konsumen yang benar-benar butuh tipe akan
    // memanggil mapType (error jujur dengan lokasi); case khusus
    // (intrinsik alokasi) memperbaiki tipe dari argumen TypeExpr.
    const TypeId resultType = typeContainsError(exprType)
                                  ? InvalidType
                                  : mapType(exprType, expr);

    switch (expr->kind) {
    case AstKind::IntegerLiteralExpr: {
        const auto* node = static_cast<const IntegerLiteralExpr*>(expr);
        if (looksLikeFloat(node->text)) {
            const bool isDouble =
                resultType != InvalidType &&
                module.types[resultType].sizeBytes == 8;
            std::uint64_t bits = 0;
            if (!parseFloatText(node->text, bits, isDouble)) {
                error(AstIrErrorCode::ConstEncodeFailed, expr,
                      "literal desimal tidak dapat di-parse: '" +
                      node->text + "'");
                return InvalidValue;
            }
            std::vector<IrOperand> operands;
            operands.push_back(IrOperand::imm(
                static_cast<std::uint32_t>(bits & 0xFFFFFFFFu)));
            if (isDouble) {
                operands.push_back(IrOperand::imm(
                    static_cast<std::uint32_t>(bits >> 32)));
            }
            return builder.emit(IrOpcode::ConstFloat, resultType,
                                std::move(operands), expr->range.begin);
        }
        std::uint64_t value = 0;
        if (!parseIntegerText(node->text, value)) {
            error(AstIrErrorCode::ConstEncodeFailed, expr,
                  "literal integer tidak dapat di-parse: '" + node->text +
                  "'");
            return InvalidValue;
        }
        return builder.emit(IrOpcode::ConstInt, resultType,
                            intOperands(value), expr->range.begin);
    }
    case AstKind::CharLiteralExpr: {
        const auto* node = static_cast<const CharLiteralExpr*>(expr);
        std::uint32_t codepoint = 0;
        if (!decodeCharLiteral(node->text, codepoint)) {
            error(AstIrErrorCode::ConstEncodeFailed, expr,
                  "literal char tidak dapat di-decode: '" + node->text + "'");
            return InvalidValue;
        }
        return builder.emit(IrOpcode::ConstInt, resultType,
                            {IrOperand::imm(codepoint)}, expr->range.begin);
    }
    case AstKind::BoolLiteralExpr: {
        const auto* node = static_cast<const BoolLiteralExpr*>(expr);
        return builder.emit(IrOpcode::ConstBool, resultType,
                            {IrOperand::imm(node->value ? 1u : 0u)},
                            expr->range.begin);
    }
    case AstKind::NullLiteralExpr:
        return builder.emit(IrOpcode::ConstNull, resultType, {},
                            expr->range.begin);
    case AstKind::StringLiteralExpr: {
        const auto* node = static_cast<const StringLiteralExpr*>(expr);
        std::string decoded;
        if (!decodeStringLiteral(*node, decoded)) {
            error(AstIrErrorCode::ConstEncodeFailed, expr,
                  "literal string tidak dapat di-decode");
            return InvalidValue;
        }
        const ConstId constant =
            encodeStringLiteral(decoded, resultType, expr);
        return builder.emit(IrOpcode::ConstString, resultType,
                            {IrOperand::constant(constant)},
                            expr->range.begin);
    }
    case AstKind::IdentifierExpr: {
        const auto* node = static_cast<const IdentifierExpr*>(expr);
        const ValueId value = lookupLocal(node->name);
        if (value != InvalidValue) return value;
        // Konstanta global (tetap): emit ulang instruksi konstanta di
        // fungsi pemakai (by-value intern per modul).
        if (const auto gc = globalConsts.find(node->name);
            gc != globalConsts.end()) {
            const GlobalConst& constant = gc->second;
            if (constant.op == IrOpcode::ConstString) {
                return builder.emit(IrOpcode::ConstString, constant.type,
                                    {IrOperand::constant(constant.poolId)},
                                    expr->range.begin, node->name);
            }
            const std::uint64_t bits = constant.bits;
            std::vector<IrOperand> operands;
            operands.push_back(IrOperand::imm(
                static_cast<std::uint32_t>(bits & 0xFFFFFFFFu)));
            if (bits > 0xFFFFFFFFu) {
                operands.push_back(IrOperand::imm(
                    static_cast<std::uint32_t>(bits >> 32)));
            }
            return builder.emit(constant.op, constant.type,
                                std::move(operands), expr->range.begin,
                                node->name);
        }
        // Enum case sebagai nilai (mis. MERAH): konstanta tag integer
        // bertipe sum. (Akses Qualified Warna::MERAH belum didukung.)
        if (exprType && exprType->kind() == TypeKind::Sum && resultType != InvalidType) {
            const auto* sum = static_cast<const SumType*>(exprType);
            for (const SumType::Case& kase : sum->cases) {
                if (kase.name == node->name) {
                    return builder.emit(
                        IrOpcode::ConstInt, resultType,
                        {IrOperand::imm(static_cast<std::uint32_t>(
                            kase.hasEnumValue ? kase.enumValue : 0))},
                        expr->range.begin, node->name);
                }
            }
        }
        // Fungsi module sebagai nilai (first-class): AddrOf @fn.
        const auto fnFound = functionsByName.find(node->name);
        if (fnFound != functionsByName.end()) {
            TypeId fnPtrType = resultType;
            if (fnPtrType == InvalidType) {
                IrType opaque;
                opaque.kind = IrTypeKind::FnPtr;
                IrType voidType;
                voidType.kind = IrTypeKind::Void;
                opaque.returnType = builder.internType(voidType);
                fnPtrType = builder.internType(opaque);
            }
            return builder.emit(IrOpcode::AddrOf, fnPtrType,
                                {IrOperand::func(fnFound->second)},
                                expr->range.begin, node->name);
        }
        error(AstIrErrorCode::MalformedShape, expr,
              "identifier '" + node->name +
              "' tidak ada di lingkungan lokal (global/qualified belum "
              "didukung)");
        return InvalidValue;
    }
    case AstKind::CallExpr: {
        const auto* node = static_cast<const CallExpr*>(expr);
        // Intrinsik isi(ptr): baca nilai pointee (deref builtin).
        if (node->callee->kind == AstKind::IdentifierExpr &&
            static_cast<const IdentifierExpr*>(node->callee)->name == "isi" &&
            node->arguments.size() == 1) {
            const ValueId addr = lowerExpr(node->arguments[0]);
            if (addr == InvalidValue) return InvalidValue;
            const TypeId ptrType =
                builder.function(currentFunc).values[addr].type;
            if (ptrType == InvalidType ||
                module.types[ptrType].kind != IrTypeKind::Pointer) {
                error(AstIrErrorCode::MalformedShape, expr,
                      "isi(ptr): argumen bukan pointer");
                return InvalidValue;
            }
            const TypeId elemType = module.types[ptrType].elementType;
            return builder.emit(IrOpcode::Load, elemType,
                                {IrOperand::value(addr)},
                                expr->range.begin);
        }
        // Konstruksi nilai sum type: callee = nama kasus, tipe hasil
        // panggilan = sum (semantic menginferensinya), argumen = payload
        // (NamedArgumentExpr nama-field atau positional).
        if (node->callee->kind == AstKind::IdentifierExpr &&
            resultType != InvalidType &&
            module.types[resultType].kind == IrTypeKind::Sum) {
            const std::string caseName =
                static_cast<const IdentifierExpr*>(node->callee)->name;
            const int variantIndex = findSumVariant(module, resultType, caseName);
            if (variantIndex >= 0) {
                // Validasi semata-mata berbasis IR tipe hasil: semantic
                // bisa memberi SumType mentah MAUPUN GenericInstantiation
                // (builtin generik terinferensi seperti Opsi<T>) — keduanya
                // sudah terpetakan ke IrType::Sum yang sama.
                const IrVariant& variant =
                    module.types[resultType].variants[variantIndex];
                std::vector<IrOperand> operands;
                operands.push_back(IrOperand::type(resultType));
                operands.push_back(IrOperand::imm(
                    static_cast<std::uint32_t>(variant.tagValue)));
                const bool expectsPayload = variant.payloadType != InvalidType;
                if (node->arguments.empty() && !expectsPayload) {
                    // tanpa payload — hanya tag
                } else if (node->arguments.size() == 1 && expectsPayload) {
                    const ExprNode* argExpr = node->arguments[0];
                    if (argExpr->kind == AstKind::NamedArgumentExpr) {
                        argExpr =
                            static_cast<const NamedArgumentExpr*>(argExpr)->value;
                    }
                    const ValueId payload = lowerExpr(argExpr);
                    if (payload == InvalidValue) return InvalidValue;
                    operands.push_back(IrOperand::value(payload));
                } else {
                    error(AstIrErrorCode::MalformedShape, expr,
                          "jumlah argumen konstruksi sum tidak cocok dengan "
                          "payload kasus (satu payload per kasus)");
                    return InvalidValue;
                }
                return builder.emit(IrOpcode::Construct, resultType,
                                    std::move(operands), expr->range.begin);
            }
        }
        // Intrinsik alokasi(T): argumen TypeExpr ATAU IdentifierExpr
        // nama struct (parser menghasilkan keduanya di konteks berbeda).
        if (node->callee->kind == AstKind::IdentifierExpr &&
            static_cast<const IdentifierExpr*>(node->callee)->name == "alokasi" &&
            node->arguments.size() == 1) {
            // Elemen alokasi dari argumen; hasil = pointer-to-elem.
            // (Tipe hasil semantic bisa ErrorType untuk call generik
            // "alokasi<T>" — inferensi tak terekam, maka tipe dibangun
            // langsung dari argumen.)
            const ExprNode* arg = node->arguments[0];
            const Type* elemSemantic = nullptr;
            if (arg->kind == AstKind::TypeExpr) {
                elemSemantic = semantic->context().getResolvedType(
                    static_cast<const TypeExpr*>(arg)->type);
            } else if (arg->kind == AstKind::IdentifierExpr) {
                elemSemantic = lookupStructType(
                    static_cast<const IdentifierExpr*>(arg)->name);
            }
            if (!elemSemantic) {
                // Argumen nilai (mis. alokasi(ukuran)): alokasi memori
                // mentah — elemen = pointee dari tipe hasil panggilan
                // (bila tercatat), selain itu void.
                if (exprType && exprType->kind() == TypeKind::Pointer) {
                    elemSemantic =
                        static_cast<const PointerType*>(exprType)->pointee;
                }
            }
            if (!elemSemantic) {
                IrType voidType;
                voidType.kind = IrTypeKind::Void;
                elemSemantic = nullptr;
                const TypeId voidId = builder.internType(voidType);
                return builder.emit(IrOpcode::Alloc, pointerTo(voidId),
                                    {IrOperand::type(voidId)},
                                    expr->range.begin);
            }
            const TypeId allocType = mapType(elemSemantic, expr);
            if (failed) return InvalidValue;
            return builder.emit(IrOpcode::Alloc, pointerTo(allocType),
                                {IrOperand::type(allocType)},
                                expr->range.begin);
        }
        // Callee non-identifier: fungsi value (fn ptr) -> CallIndirect.
        if (node->callee->kind != AstKind::IdentifierExpr) {
            const ValueId fnPtr = lowerExpr(node->callee);
            if (fnPtr == InvalidValue) return InvalidValue;
            std::vector<IrOperand> indirectOps;
            indirectOps.push_back(IrOperand::value(fnPtr));
            for (const ExprNode* arg : node->arguments) {
                if (arg->kind == AstKind::NamedArgumentExpr &&
                    static_cast<const NamedArgumentExpr*>(arg)->name ==
                        "dengan") {
                    continue;
                }
                const ValueId value =
                    lowerExpr(arg->kind == AstKind::NamedArgumentExpr
                                  ? static_cast<const NamedArgumentExpr*>(arg)->value
                                  : arg);
                if (value == InvalidValue) return InvalidValue;
                indirectOps.push_back(IrOperand::value(value));
            }
            return builder.emit(IrOpcode::CallIndirect, resultType,
                                std::move(indirectOps), expr->range.begin);
        }
        const FuncId callee = resolveCallee(node->callee);
        if (callee == InvalidFunc) return InvalidValue;
        std::vector<IrOperand> operands;
        operands.push_back(IrOperand::func(callee));
        for (const ExprNode* arg : node->arguments) {
            if (arg->kind == AstKind::NamedArgumentExpr) {
                const auto* named =
                    static_cast<const NamedArgumentExpr*>(arg);
                if (named->name == "dengan") {
                    continue;  // type argument generik, bukan nilai
                }
                const ValueId value = lowerExpr(named->value);
                if (value == InvalidValue) return InvalidValue;
                operands.push_back(IrOperand::value(value));
            } else {
                const ValueId value = lowerExpr(arg);
                if (value == InvalidValue) return InvalidValue;
                operands.push_back(IrOperand::value(value));
            }
        }
        // Fallback tipe: bila inferensi semantic memberi ErrorType
        // tanpa diagnostic (temuan RhSemantic — call builtin terdaftar),
        // resolve tipe hasil dari symbol global scope (FunctionType
        // builtin). Tanpa fallback ini call ditolak jujur di kontrak
        // di bawah.
        TypeId callResultType = resultType;
        if (callResultType == InvalidType && exprType &&
            typeContainsError(exprType) &&
            node->callee->kind == AstKind::IdentifierExpr) {
            if (const Symbol* symbol = lookupGlobalSymbol(
                    static_cast<const IdentifierExpr*>(node->callee)->name)) {
                if (symbol->type &&
                    symbol->type->kind() == TypeKind::Function) {
                    const auto* fnType =
                        static_cast<const FunctionType*>(symbol->type);
                    // ReturnType valid dipakai; null ATAU Error (builtin
                    // void yang inferensinya gagal — temuan RhSemantic)
                    // dianggap Hampa: call prosedur tanpa nilai hasil.
                    if (fnType->returnType &&
                        !typeContainsError(fnType->returnType)) {
                        callResultType = mapType(fnType->returnType, expr);
                    } else {
                        IrType voidType;
                        voidType.kind = IrTypeKind::Void;
                        callResultType = builder.internType(voidType);
                    }
                }
            }
        }
        const ValueId callResult =
            builder.emit(IrOpcode::Call, callResultType,
                         std::move(operands), expr->range.begin);
        if (callResult == InvalidValue && !builder.broken() && !failed &&
            callResultType == InvalidType) {
            // Instruksi ber-result tanpa tipe yang dapat dipetakan —
            // mis. panggilan fungsi tak dikenal yang dibiarkan semantic
            // dengan ErrorType. Kontrak: kegagalan harus jujur.
            std::string calleeName;
            if (node->callee->kind == AstKind::IdentifierExpr) {
                calleeName =
                    static_cast<const IdentifierExpr*>(node->callee)->name;
            } else {
                calleeName = "<non-identifier>";
            }
            error(AstIrErrorCode::TypeMappingFailed, expr,
                  "tipe hasil panggilan '" + calleeName + "' tidak dapat "
                  "dipetakan ke IR (fungsi tak dikenal / inferensi "
                  "semantic memberi ErrorType tanpa diagnostic)");
        }
        return callResult;
    }
    case AstKind::BinaryExpr: {
        const auto* node = static_cast<const BinaryExpr*>(expr);

        // Assignment ke member (a.b.c = v): persistent update.
        if (node->op == TokenKind::Equal &&
            node->lhs->kind == AstKind::MemberExpr) {
            return lowerMemberWrite(node->lhs, node->rhs);
        }

        // Assignment ke elemen larik (a[i] = v): Insert + rebind.
        if (node->op == TokenKind::Equal &&
            node->lhs->kind == AstKind::ArrayIndexExpr) {
            return lowerArrayIndexWrite(
                static_cast<const ArrayIndexExpr*>(node->lhs), node->rhs);
        }

        // Assignment via dereference: isi(ptr) = v -> Store(ptr, v).
        // "isi" adalah fungsi builtin deref (CallExpr, bukan unary op).
        if (node->op == TokenKind::Equal &&
            node->lhs->kind == AstKind::CallExpr &&
            static_cast<const CallExpr*>(node->lhs)->callee->kind ==
                AstKind::IdentifierExpr &&
            static_cast<const IdentifierExpr*>(
                static_cast<const CallExpr*>(node->lhs)->callee)->name ==
                "isi" &&
            static_cast<const CallExpr*>(node->lhs)->arguments.size() == 1) {
            const ValueId addr = lowerExpr(
                static_cast<const CallExpr*>(node->lhs)->arguments[0]);
            if (addr == InvalidValue) return InvalidValue;
            const ValueId value = lowerExpr(node->rhs);
            if (value == InvalidValue) return InvalidValue;
            builder.emit(IrOpcode::Store, InvalidType,
                         {IrOperand::value(addr), IrOperand::value(value)},
                         expr->range.begin);
            return builder.broken() ? InvalidValue : value;
        }

        // Assignment (=): versioning variabel lokal.
        if (node->op == TokenKind::Equal) {
            if (node->lhs->kind != AstKind::IdentifierExpr) {
                error(AstIrErrorCode::UnsupportedNode, expr,
                      "assignment ke lvalue non-identifier (member/indeks) "
                      "belum didukung lowering");
                return InvalidValue;
            }
            const auto* target =
                static_cast<const IdentifierExpr*>(node->lhs);
            const ValueId value = lowerExpr(node->rhs);
            if (value == InvalidValue) return InvalidValue;
            if (lookupRaw(target->name) == InvalidValue &&
                module.globalVarIndex.count(target->name) == 0) {
                error(AstIrErrorCode::MalformedShape, expr,
                      "assignment ke '" + target->name +
                      "' yang belum dideklarasikan");
                return InvalidValue;
            }
            if (isMemoryStyle(target->name)) {
                builder.emit(IrOpcode::Store, InvalidType,
                             {IrOperand::value(lookupRaw(target->name)),
                              IrOperand::value(value)},
                             expr->range.begin);
                return builder.broken() ? InvalidValue : value;
            }
            const auto globalTarget = module.globalVarIndex.find(target->name);
            if (globalTarget != module.globalVarIndex.end()) {
                builder.emit(IrOpcode::Store, InvalidType,
                             {IrOperand::global(globalTarget->second),
                              IrOperand::value(value)},
                             expr->range.begin);
                return builder.broken() ? InvalidValue : value;
            }
            bindLocal(target->name, value);
            return value;
        }

        // Compound assignment (+=, -=, &=, |=, ^=, <<=, ...):
        // load-modify-store untuk target identifier.
        const IrOpcode compound = compoundOpcode(node->op);
        if (compound != IrOpcode::Dead) {
            // Compound ke member (a.b += v) / elemen larik (a[i] += v):
            // read-modify-write via jalur agregat.
            if (node->lhs->kind == AstKind::MemberExpr) {
                const ValueId rhs0 = lowerExpr(node->rhs);
                if (rhs0 == InvalidValue) return InvalidValue;
                const ValueId current = lowerMemberRead(node->lhs);
                if (current == InvalidValue) return InvalidValue;
                const TypeId lhsType = mapType(
                    semantic->context().getExprType(node->lhs), expr,
                    "compound-lhs");
                const ValueId updated = builder.emit(
                    compound, lhsType,
                    {IrOperand::value(current), IrOperand::value(rhs0)},
                    expr->range.begin);
                return lowerMemberWriteValue(node->lhs, updated);
            }
            // Compound via dereference (isi(ptr) += v): Load-op-Store.
            if (node->lhs->kind == AstKind::CallExpr &&
                static_cast<const CallExpr*>(node->lhs)->callee->kind ==
                    AstKind::IdentifierExpr &&
                static_cast<const IdentifierExpr*>(
                    static_cast<const CallExpr*>(node->lhs)->callee)->name ==
                    "isi" &&
                static_cast<const CallExpr*>(node->lhs)->arguments.size() == 1) {
                const ValueId addr = lowerExpr(
                    static_cast<const CallExpr*>(node->lhs)->arguments[0]);
                if (addr == InvalidValue) return InvalidValue;
                const ValueId rhs0 = lowerExpr(node->rhs);
                if (rhs0 == InvalidValue) return InvalidValue;
                const TypeId ptrType =
                    builder.function(currentFunc).values[addr].type;
                const TypeId elemType = module.types[ptrType].elementType;
                const ValueId current = builder.emit(
                    IrOpcode::Load, elemType, {IrOperand::value(addr)},
                    expr->range.begin);
                const ValueId updated = builder.emit(
                    compound, elemType,
                    {IrOperand::value(current), IrOperand::value(rhs0)},
                    expr->range.begin);
                builder.emit(IrOpcode::Store, InvalidType,
                             {IrOperand::value(addr),
                              IrOperand::value(updated)},
                             expr->range.begin);
                return builder.broken() ? InvalidValue : updated;
            }
            if (node->lhs->kind == AstKind::ArrayIndexExpr) {
                const auto* indexTarget =
                    static_cast<const ArrayIndexExpr*>(node->lhs);
                const ValueId rhs0 = lowerExpr(node->rhs);
                if (rhs0 == InvalidValue) return InvalidValue;
                const ValueId current = lowerArrayIndexRead(indexTarget);
                if (current == InvalidValue) return InvalidValue;
                const TypeId lhsType = mapType(
                    semantic->context().getExprType(node->lhs), expr,
                    "compound-lhs");
                const ValueId updated = builder.emit(
                    compound, lhsType,
                    {IrOperand::value(current), IrOperand::value(rhs0)},
                    expr->range.begin);
                return lowerArrayIndexWriteValue(indexTarget, updated);
            }
            const auto* target =
                static_cast<const IdentifierExpr*>(node->lhs);
            if (node->lhs->kind != AstKind::IdentifierExpr) {
                error(AstIrErrorCode::UnsupportedNode, expr,
                      "compound assignment ke lvalue non-identifier belum "
                      "didukung lowering");
                return InvalidValue;
            }
            const ValueId current = lookupLocal(target->name);
            if (current == InvalidValue) {
                error(AstIrErrorCode::MalformedShape, expr,
                      "compound assignment ke '" + target->name +
                      "' yang belum dideklarasikan");
                return InvalidValue;
            }
            if (isMemoryStyle(target->name)) {
                const ValueId rhs0 = lowerExpr(node->rhs);
                if (rhs0 == InvalidValue) return InvalidValue;
                const TypeId lhsType =
                    mapType(semantic->context().getExprType(node->lhs),
                            expr, "compound-lhs");
                const ValueId updated = builder.emit(compound, lhsType,
                    {IrOperand::value(current), IrOperand::value(rhs0)},
                    expr->range.begin, target->name);
                builder.emit(IrOpcode::Store, InvalidType,
                             {IrOperand::value(lookupRaw(target->name)),
                              IrOperand::value(updated)},
                             expr->range.begin);
                return builder.broken() ? InvalidValue : updated;
            }
            if (module.globalVarIndex.count(target->name)) {
                const ValueId rhs0 = lowerExpr(node->rhs);
                if (rhs0 == InvalidValue) return InvalidValue;
                const TypeId lhsType =
                    mapType(semantic->context().getExprType(node->lhs),
                            expr, "compound-lhs");
                const ValueId updated = builder.emit(compound, lhsType,
                    {IrOperand::value(current), IrOperand::value(rhs0)},
                    expr->range.begin, target->name);
                builder.emit(IrOpcode::Store, InvalidType,
                             {IrOperand::global(module.globalVarIndex[target->name]),
                              IrOperand::value(updated)},
                             expr->range.begin);
                return builder.broken() ? InvalidValue : updated;
            }
            const ValueId rhs = lowerExpr(node->rhs);
            if (rhs == InvalidValue) return InvalidValue;
            const TypeId lhsType = mapType(semantic->context().getExprType(node->lhs),
                                           expr, "compound-lhs2");
            const ValueId updated = builder.emit(compound, lhsType,
                {IrOperand::value(current), IrOperand::value(rhs)},
                expr->range.begin, target->name);
            bindLocal(target->name, updated);
            return updated;
        }

        const IrOpcode opcode = binaryOpcode(node->op);
        if (opcode == IrOpcode::Dead) {
            error(AstIrErrorCode::UnsupportedNode, expr,
                  "operator binary belum dipetakan ke opcode IR");
            return InvalidValue;
        }
        const ValueId lhs = lowerExpr(node->lhs);
        if (lhs == InvalidValue) return InvalidValue;
        const ValueId rhs = lowerExpr(node->rhs);
        if (rhs == InvalidValue) return InvalidValue;
        return builder.emit(opcode, resultType,
                            {IrOperand::value(lhs), IrOperand::value(rhs)},
                            expr->range.begin);
    }
    case AstKind::UnaryExpr: {
        const auto* node = static_cast<const UnaryExpr*>(expr);
        switch (node->op) {
        case UnaryOp::Plus:
            return lowerExpr(node->operand);
        case UnaryOp::Minus:
        case UnaryOp::LogicalNot:
        case UnaryOp::BitNot: {
            const ValueId operand = lowerExpr(node->operand);
            if (operand == InvalidValue) return InvalidValue;
            const IrOpcode opcode = node->op == UnaryOp::Minus
                                        ? IrOpcode::Neg
                                        : node->op == UnaryOp::LogicalNot
                                              ? IrOpcode::Not
                                              : IrOpcode::BitNot;
            return builder.emit(opcode, resultType,
                                {IrOperand::value(operand)},
                                expr->range.begin);
        }
        case UnaryOp::Increment:
        case UnaryOp::Decrement: {
            if (node->operand->kind != AstKind::IdentifierExpr) {
                error(AstIrErrorCode::UnsupportedNode, expr,
                      "++/-- pada lvalue non-identifier belum didukung");
                return InvalidValue;
            }
            const auto* target =
                static_cast<const IdentifierExpr*>(node->operand);
            const ValueId current = lookupLocal(target->name);
            if (current == InvalidValue) {
                error(AstIrErrorCode::MalformedShape, expr,
                      "++/-- pada '" + target->name +
                      "' yang belum dideklarasikan");
                return InvalidValue;
            }
            const IrOpcode opcode = node->op == UnaryOp::Increment
                                        ? IrOpcode::Inc
                                        : IrOpcode::Dec;
            const ValueId updated = builder.emit(opcode,
                mapType(semantic->context().getExprType(node->operand)),
                {IrOperand::value(current)}, expr->range.begin,
                target->name);
            bindLocal(target->name, updated);
            return updated;
        }
        case UnaryOp::AddressOf: {
            // Variabel memory-style: alamatnya = ptr Alloc yang tersimpan.
            // Operand lain (member/temp) belum didukung.
            if (node->operand->kind != AstKind::IdentifierExpr) {
                error(AstIrErrorCode::UnsupportedNode, expr,
                      "alamat (&) hanya didukung untuk variabel");
                return InvalidValue;
            }
            const auto* target =
                static_cast<const IdentifierExpr*>(node->operand);
            if (!isMemoryStyle(target->name)) {
                error(AstIrErrorCode::UnsupportedNode, expr,
                      "alamat (&) pada variabel non-memory-style: pre-pass "
                      "address-taken gagal mendeteksi");
                return InvalidValue;
            }
            return lookupRaw(target->name);
        }
        case UnaryOp::Await:
            error(AstIrErrorCode::UnsupportedNode, expr,
                  "await ditolak validasi fase 1");
            return InvalidValue;
        }
        error(AstIrErrorCode::UnsupportedNode, expr,
              "operator unary belum dipetakan");
        return InvalidValue;
    }
    case AstKind::TernaryExpr:
        return lowerTernaryExpr(static_cast<const TernaryExpr*>(expr));
    case AstKind::MemberExpr:
        return lowerMemberRead(expr);
    case AstKind::ArrayLiteralExpr:
        return lowerArrayLiteral(static_cast<const ArrayLiteralExpr*>(expr));
    case AstKind::ArrayIndexExpr:
        return lowerArrayIndexRead(static_cast<const ArrayIndexExpr*>(expr));
    case AstKind::SliceExpr:
    case AstKind::TypeExpr:
        error(AstIrErrorCode::UnsupportedNode, expr,
              "slice/type-expression belum di-lowering (fase view)");
        return InvalidValue;
    case AstKind::LambdaExpr:
        error(AstIrErrorCode::UnsupportedNode, expr,
              "lambda ditolak validasi fase 1");
        return InvalidValue;
    default:
        error(AstIrErrorCode::UnsupportedNode, expr,
              "jenis ekspresi belum di-lowering");
        return InvalidValue;
    }
}

} // namespace rhc
