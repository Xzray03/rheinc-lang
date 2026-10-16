#include "AstToIrTranslatorImpl.h"

#include "RhLexer/TokenKind.h"
#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

#include <cstdint>
#include <cstdlib>
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

bool parseIntegerText(const std::string& text, std::uint64_t& out) {
    std::string cleaned;
    cleaned.reserve(text.size());
    for (char ch : text) {
        if (ch != '_') cleaned.push_back(ch);
    }
    if (cleaned.empty()) return false;
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
    if (failed || builder.broken()) return InvalidValue;
    const Type* exprType = semantic->context().getExprType(expr);
    // ErrorType ditunda pemetaannya: konsumen yang benar-benar butuh
    // tipe akan memanggil mapType (error jujur dengan lokasi); case
    // khusus (intrinsik alokasi) memperbaiki tipe dari argumen TypeExpr.
    const TypeId resultType =
        (exprType && exprType->kind() == TypeKind::Error)
            ? InvalidType
            : mapType(exprType, expr);

    switch (expr->kind) {
    case AstKind::IntegerLiteralExpr: {
        const auto* node = static_cast<const IntegerLiteralExpr*>(expr);
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
        if (value == InvalidValue) {
            error(AstIrErrorCode::MalformedShape, expr,
                  "identifier '" + node->name +
                  "' tidak ada di lingkungan lokal (global/belum lowering?)");
            return InvalidValue;
        }
        return value;
    }
    case AstKind::CallExpr: {
        const auto* node = static_cast<const CallExpr*>(expr);
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
                error(AstIrErrorCode::UnsupportedNode, expr,
                      "alokasi(T): argumen bukan tipe yang dikenal");
                return InvalidValue;
            }
            const TypeId allocType = mapType(elemSemantic, expr);
            if (failed) return InvalidValue;
            return builder.emit(IrOpcode::Alloc, pointerTo(allocType),
                                {IrOperand::type(allocType)},
                                expr->range.begin);
        }
        const FuncId callee = resolveCallee(node->callee);
        if (callee == InvalidFunc) return InvalidValue;
        std::vector<IrOperand> operands;
        operands.push_back(IrOperand::func(callee));
        for (const ExprNode* arg : node->arguments) {
            if (arg->kind == AstKind::NamedArgumentExpr) {
                const auto* named =
                    static_cast<const NamedArgumentExpr*>(arg);
                const ValueId value = lowerExpr(named->value);
                if (value == InvalidValue) return InvalidValue;
                operands.push_back(IrOperand::value(value));
            } else {
                const ValueId value = lowerExpr(arg);
                if (value == InvalidValue) return InvalidValue;
                operands.push_back(IrOperand::value(value));
            }
        }
        return builder.emit(IrOpcode::Call, resultType, std::move(operands),
                            expr->range.begin);
    }
    case AstKind::BinaryExpr: {
        const auto* node = static_cast<const BinaryExpr*>(expr);

        // Assignment ke member (a.b.c = v): persistent update.
        if (node->op == TokenKind::Equal &&
            node->lhs->kind == AstKind::MemberExpr) {
            return lowerMemberWrite(node->lhs, node->rhs);
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
            if (lookupRaw(target->name) == InvalidValue) {
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
            bindLocal(target->name, value);
            return value;
        }

        // Compound assignment (+=, -=, ...): load-modify-store untuk
        // target identifier.
        const IrOpcode compound = binaryOpcode(node->op);
        if (node->op != TokenKind::Equal && compound != IrOpcode::Dead &&
            node->op >= TokenKind::PlusEqual &&
            node->op <= TokenKind::GreaterGreaterEqual) {
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
                    mapType(semantic->context().getExprType(node->lhs));
                const ValueId updated = builder.emit(compound, lhsType,
                    {IrOperand::value(current), IrOperand::value(rhs0)},
                    expr->range.begin, target->name);
                builder.emit(IrOpcode::Store, InvalidType,
                             {IrOperand::value(lookupRaw(target->name)),
                              IrOperand::value(updated)},
                             expr->range.begin);
                return builder.broken() ? InvalidValue : updated;
            }
            const ValueId rhs = lowerExpr(node->rhs);
            if (rhs == InvalidValue) return InvalidValue;
            const TypeId lhsType = mapType(semantic->context().getExprType(node->lhs));
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
        error(AstIrErrorCode::UnsupportedNode, expr,
              "ternary butuh CFG cabang — diimplementasi bersama fase "
              "statement (InsertPhiAtJoin)");
        return InvalidValue;
    case AstKind::MemberExpr:
        return lowerMemberRead(expr);
    case AstKind::ArrayLiteralExpr:
    case AstKind::ArrayIndexExpr:
    case AstKind::SliceExpr:
    case AstKind::TypeExpr:
        error(AstIrErrorCode::UnsupportedNode, expr,
              "ekspresi agregat/index/slice belum di-lowering");
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
