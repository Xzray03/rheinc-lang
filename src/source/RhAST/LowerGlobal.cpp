#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Decl.h"
#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"

#include <cstdint>

namespace rhc {

// Konstanta global (tetap): initializer WAJIB literal skalar
// (integer/char/bool/string) — dinilai saat translasi, dibaca
// by-value di setiap fungsi pemakai.
bool AstToIrTranslator::Impl::lowerGlobalConst(const GlobalVarDecl* decl) {
    const ExprNode* init = decl->initializer;
    if (!init) {
        return error(AstIrErrorCode::MalformedShape, decl,
                     "konstanta global tanpa initializer");
    }
    const TypeId type =
        mapType(semantic->context().getResolvedType(decl->type), decl);
    if (failed) return false;

    GlobalConst constant;
    constant.type = type;
    switch (init->kind) {
    case AstKind::IntegerLiteralExpr: {
        const auto* literal = static_cast<const IntegerLiteralExpr*>(init);
        // Literal integer — angka murni saja (bukan desimal; float
        // global masuk fase berikutnya).
        std::uint64_t value = 0;
        std::string cleaned;
        for (char ch : literal->text) {
            if (ch != '_') cleaned.push_back(ch);
        }
        try {
            std::size_t pos = 0;
            value = std::stoull(cleaned, &pos, 0);
            if (pos != cleaned.size()) {
                return error(AstIrErrorCode::ConstEncodeFailed, init,
                             "konstanta global integer tidak dapat "
                             "di-parse: '" + literal->text + "'");
            }
        } catch (...) {
            return error(AstIrErrorCode::ConstEncodeFailed, init,
                         "konstanta global integer tidak dapat di-parse");
        }
        constant.op = IrOpcode::ConstInt;
        constant.bits = value;
        break;
    }
    case AstKind::BoolLiteralExpr: {
        const auto* literal = static_cast<const BoolLiteralExpr*>(init);
        constant.op = IrOpcode::ConstBool;
        constant.bits = literal->value ? 1 : 0;
        break;
    }
    case AstKind::StringLiteralExpr: {
        const auto* literal = static_cast<const StringLiteralExpr*>(init);
        // decode sederhana: string global diasumsikan tanpa escape
        // kompleks (escape penuh sama dengan decoder LowerExpr).
        std::string decoded;
        if (literal->text.size() < 2) {
            return error(AstIrErrorCode::ConstEncodeFailed, init,
                         "literal string global rusak");
        }
        for (std::size_t i = 1; i + 1 < literal->text.size(); ++i) {
            if (literal->text[i] == '\\' && !literal->raw &&
                i + 2 < literal->text.size()) {
                switch (literal->text[i + 1]) {
                case 'n': decoded.push_back('\n'); ++i; break;
                case 't': decoded.push_back('\t'); ++i; break;
                case 'r': decoded.push_back('\r'); ++i; break;
                case '\\': decoded.push_back('\\'); ++i; break;
                case '"': decoded.push_back('"'); ++i; break;
                default:
                    return error(AstIrErrorCode::ConstEncodeFailed, init,
                                 "escape string global tidak dikenal");
                }
            } else {
                decoded.push_back(literal->text[i]);
            }
        }
        constant.op = IrOpcode::ConstString;
        constant.poolId = encodeStringLiteral(decoded, type, init);
        break;
    }
    case AstKind::CharLiteralExpr: {
        const auto* literal = static_cast<const CharLiteralExpr*>(init);
        // codepoint pertama (UTF-8 ASCII cukup untuk corpus)
        if (literal->text.size() < 3) {
            return error(AstIrErrorCode::ConstEncodeFailed, init,
                         "literal char global rusak");
        }
        constant.op = IrOpcode::ConstInt;
        constant.bits = static_cast<unsigned char>(literal->text[1]);
        break;
    }
    default:
        return error(AstIrErrorCode::UnsupportedNode, init,
                     "initializer konstanta global harus literal skalar "
                     "(fase ini: integer/char/bool/string)");
    }

    if (const Symbol* symbol =
            semantic->context().getDeclSymbol(decl)) {
        module.symbolMapping.mapGlobal(symbol, InvalidValue);  // placeholder
    }
    globalConsts[decl->name] = constant;
    return true;
}

// Global mutable: daftarkan storage; initializer (literal maupun
// non-literal) dikerjakan .__global_init (lowerGlobalInitFunction).
bool AstToIrTranslator::Impl::lowerGlobalMutable(const GlobalVarDecl* decl) {
    const TypeId type =
        mapType(semantic->context().getResolvedType(decl->type), decl);
    if (failed) return false;

    IrGlobal global;
    global.name = decl->name;
    global.type = type;
    global.loc = decl->range.begin;
    module.globalVars.push_back(std::move(global));
    const std::uint32_t index =
        static_cast<std::uint32_t>(module.globalVars.size() - 1);
    module.globalVarIndex[decl->name] = index;
    if (const Symbol* symbol = semantic->context().getDeclSymbol(decl)) {
        module.symbolMapping.mapGlobal(symbol, index);  // payload = idx global
    }
    if (decl->initializer) {
        globalInitDecls.push_back(decl);
    }
    return true;
}

// Bangun fungsi implisit .__global_init: tiap global mutable diinisialisasi
// dengan Store(@Gi, value) — value dilowering normal (call/alokasi/binary
// semua jalan karena ini fungsi biasa).
bool AstToIrTranslator::Impl::lowerGlobalInitFunction() {
    if (globalInitDecls.empty()) return true;

    const FuncId func = builder.createFunction(".__global_init", InvalidType, 0,
                                                 IrFunctionKind::GlobalInit);

    IrType voidType;
    voidType.kind = IrTypeKind::Void;
    const TypeId voidTypeId = builder.internType(voidType);
    IrType initFnType;
    initFnType.kind = IrTypeKind::FnPtr;
    initFnType.returnType = voidTypeId;
    builder.function(func).fnType = builder.internType(initFnType);

    const FuncId previousFunc = currentFunc;
    currentFunc = func;
    env.clear();
    pushEnv();
    const BlockId entry = builder.createBlock(func, "global.init");
    setCurrent(entry);

    for (const GlobalVarDecl* decl : globalInitDecls) {
        const ValueId value = lowerExpr(decl->initializer);
        if (value == InvalidValue) return false;
        const auto found = module.globalVarIndex.find(decl->name);
        builder.emit(IrOpcode::Store, InvalidType,
                     {IrOperand::global(found->second),
                      IrOperand::value(value)},
                     decl->range.begin);
    }
    builder.setTerminator(func, entry, IrOpcode::RetVoid, {}, 0);
    globalInitDecls.clear();
    popEnv();
    currentFunc = previousFunc;
    currentBlock = InvalidBlock;
    return !builder.broken();
}

} // namespace rhc
