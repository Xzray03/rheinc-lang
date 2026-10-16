#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Stmt.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

namespace rhc {

bool AstToIrTranslator::Impl::lowerStmt(const StmtNode* stmt) {
    if (failed || builder.broken()) return false;

    // Skip statement di jalur tak tercapai (setelah return/break/goto).
    // LabelStmt tetap diproses agar block-nya terdaftar.
    if (!blockOpen() && stmt->kind != AstKind::LabelStmt) return true;

    switch (stmt->kind) {
    case AstKind::CompoundStmt: {
        const auto* node = static_cast<const CompoundStmt*>(stmt);
        pushEnv();
        for (const StmtNode* child : node->statements) {
            if (!lowerStmt(child)) { popEnv(); return false; }
        }
        // Scope exit normal: ekspansi jamin scope ini (FIFO, D-1).
        if (blockOpen()) {
            emitScopeDefers(env.size() - 1);
        }
        popEnv();
        return true;
    }
    case AstKind::DeclStmt: {
        // Deklarasi struct/enum di dalam scope fungsi: tipe dipetakan
        // lazily saat dipakai (resolve lewat RhSemantic) — tidak ada
        // instruksi yang dihasilkan.
        const auto* node = static_cast<const DeclStmt*>(stmt);
        const DeclNode* innerDecl = node->declaration;
        if (innerDecl &&
            (innerDecl->kind == AstKind::StructDecl ||
             innerDecl->kind == AstKind::EnumDecl)) {
            return true;
        }
        return error(AstIrErrorCode::UnsupportedNode, stmt,
                     "DeclStmt berisi deklarasi yang belum didukung "
                     "di dalam fungsi");
    }
    case AstKind::ExprStmt: {
        const auto* node = static_cast<const ExprStmt*>(stmt);
        return lowerExpr(node->expression) != InvalidValue;
    }
    case AstKind::ReturnStmt: {
        const auto* node = static_cast<const ReturnStmt*>(stmt);
        emitAllDefers();  // semua jamin aktif jalan sebelum kembalikan
        if (failed || builder.broken()) return false;
        if (node->value) {
            const ValueId value = lowerExpr(node->value);
            if (value == InvalidValue) return false;
            builder.setTerminator(currentFunc, currentBlock, IrOpcode::Ret,
                                  {IrOperand::value(value)}, stmt->range.begin);
        } else {
            builder.setTerminator(currentFunc, currentBlock, IrOpcode::RetVoid,
                                  {}, stmt->range.begin);
        }
        setCurrent(InvalidBlock);
        return !builder.broken();
    }
    case AstKind::VarDeclStmt: {
        const auto* node = static_cast<const VarDeclStmt*>(stmt);
        const Type* semanticType = nullptr;
        if (node->initializer) {
            semanticType = exprTypeOr(node->initializer, node->type);
        } else if (node->type) {
            semanticType = semantic->context().getResolvedType(node->type);
        }
        // Tipe Error (mis. anotasi hasil "-> tipe" tanpa deklarasi):
        // tipe diambil dari nilai initializer setelah lowering.
        const bool deferVarType =
            node->initializer && semanticType &&
            typeContainsError(semanticType);
        const TypeId varType = deferVarType ? InvalidType
                                            : mapType(semanticType, stmt);
        if (failed) return false;

        // Variabel yang di-alamati -> memory-style: Alloc + Store.
        // Nilai tersimpan = ptr; pembacaan via Load (lookupLocal).
        if (isMemoryStyle(node->name)) {
            IrType ptrIr;
            ptrIr.kind = IrTypeKind::Pointer;
            ptrIr.elementType = varType;
            ptrIr.sizeBytes = 8;
            ptrIr.alignBytes = 8;
            const TypeId ptrType = builder.internType(ptrIr);
            const ValueId ptr = builder.emit(IrOpcode::Alloc, ptrType,
                                             {IrOperand::type(varType)},
                                             stmt->range.begin, node->name);
            ValueId initial = InvalidValue;
            if (node->initializer) {
                initial = lowerExpr(node->initializer);
            } else {
                initial = emitDefaultValue(varType, semanticType,
                                           stmt->range.begin);
            }
            if (initial == InvalidValue) {
                if (!failed && !builder.broken()) {
                    return error(AstIrErrorCode::TypeMappingFailed, stmt,
                                 "initializer '" + node->name + "' tidak "
                                 "menghasilkan nilai IR yang valid");
                }
                return false;
            }
            builder.emit(IrOpcode::Store, InvalidType,
                         {IrOperand::value(ptr), IrOperand::value(initial)},
                         stmt->range.begin);
            if (builder.broken()) return false;
            bindLocal(node->name, ptr);
            return true;
        }

        ValueId initial = InvalidValue;
        if (node->initializer) {
            initial = lowerExpr(node->initializer);
            if (initial == InvalidValue) {
                if (!failed && !builder.broken()) {
                    return error(AstIrErrorCode::TypeMappingFailed, stmt,
                                 "initializer '" + node->name + "' tidak "
                                 "menghasilkan nilai IR yang valid");
                }
                return false;
            }
        } else {
            // Default value eksplisit (zero/empty/construct), bukan
            // ConstInt 0 mentah — tipe non-integer butuh representasi
            // default yang sesuai (spesifikasi: verbose & eksplisit).
            initial = emitDefaultValue(varType, semanticType,
                                       stmt->range.begin);
            if (initial == InvalidValue && !failed) {
                return error(AstIrErrorCode::UnsupportedNode, stmt,
                             "default value tipe variabel belum didukung");
            }
        }
        if (builder.broken()) return false;
        (void)varType;
        bindLocal(node->name, initial);
        return true;
    }
    case AstKind::IfStmt:
        return lowerIfStatement(static_cast<const IfStmt*>(stmt));
    case AstKind::ForStmt:
        return lowerForStatement(static_cast<const ForStmt*>(stmt));
    case AstKind::WhileStmt:
        return lowerWhileStatement(static_cast<const WhileStmt*>(stmt));
    case AstKind::DoWhileStmt:
        return lowerDoWhileStatement(static_cast<const DoWhileStmt*>(stmt));
    case AstKind::BreakStmt:
        return lowerBreak();
    case AstKind::ContinueStmt:
        return lowerContinue();
    case AstKind::GotoStmt: {
        const auto* node = static_cast<const GotoStmt*>(stmt);
        const BlockId bridge =
            builder.createBlock(currentFunc, "goto." + node->label);
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                              {IrOperand::block(bridge)}, stmt->range.begin);
        pendingGotos.push_back({bridge, node->label, stmt->range.begin});
        setCurrent(InvalidBlock);
        return !builder.broken();
    }
    case AstKind::LabelStmt: {
        const auto* node = static_cast<const LabelStmt*>(stmt);
        BlockId labelBlock = InvalidBlock;
        const auto found = labelBlocks.find(node->label);
        if (found != labelBlocks.end()) {
            labelBlock = found->second;
        } else {
            labelBlock = builder.createBlock(currentFunc, node->label);
            labelBlocks[node->label] = labelBlock;
        }
        if (blockOpen()) {
            builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                                  {IrOperand::block(labelBlock)},
                                  stmt->range.begin);
        }
        setCurrent(labelBlock);
        if (!lowerStmt(node->statement)) return false;
        return true;
    }
    case AstKind::JedaStmt: {
        // jeda / jeda sampai kondisi: sync-structural (DEVIATIONS D-3) —
        // kondisi tetap dievaluasi untuk side effects; suspensi sesungguhnya
        // (state machine korutin) adalah fase berikutnya.
        const auto* node = static_cast<const JedaStmt*>(stmt);
        if (node->untilCondition) {
            if (lowerExpr(node->untilCondition) == InvalidValue) return false;
        }
        return true;
    }
    case AstKind::JaminStmt: {
        const auto* node = static_cast<const JaminStmt*>(stmt);
        if (!node->body || node->body->kind != AstKind::CompoundStmt) {
            return error(AstIrErrorCode::MalformedShape, stmt,
                         "jamin tanpa body CompoundStmt");
        }
        env.back().defers.push_back(node->body);
        return true;
    }
    case AstKind::PilihStmt:
        return lowerPilihStatement(static_cast<const PilihStmt*>(stmt));
    case AstKind::KeluarStmt: {
        // keluar proses [kode]: panggil keluar(kode) lalu Unreachable.
        const auto* node = static_cast<const KeluarStmt*>(stmt);
        ValueId code = InvalidValue;
        if (node->value) {
            code = lowerExpr(node->value);
            if (code == InvalidValue) return false;
        } else {
            code = builder.emit(IrOpcode::ConstInt, InvalidType,
                                {IrOperand::imm(0)}, stmt->range.begin);
        }
        (void)emitCallByName("keluar", {code}, stmt->range.begin);
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Unreachable,
                              {}, stmt->range.begin);
        setCurrent(InvalidBlock);
        return !builder.broken();
    }
    case AstKind::CaseClause:
    case AstKind::ErrorStmt:
        return error(AstIrErrorCode::UnsupportedNode, stmt,
                     "statement ini tidak lolos validasi fase 1");
    default:
        return error(AstIrErrorCode::UnsupportedNode, stmt,
                     "jenis statement belum di-lowering");
    }
}

} // namespace rhc
