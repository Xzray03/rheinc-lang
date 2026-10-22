#include <cstdio>
#include <functional>
#include "RhAST/AstToIrTranslatorImpl.h"

// Decode string literal (didefinisikan di LowerExpr.cpp, namespace rhc)
// — dipakai cabang konstruksi object dengan named-args (spec §15.3).
namespace rhc {
bool decodeStringLiteral(const StringLiteralExpr& node, std::string& out);
}
using rhc::decodeStringLiteral;

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
        // Anotasi tipe deklarasi DIPAKAI bila ada (konversi implisit ke
        // tipe yang dinyatakan); inference dari initializer hanya fallback
        // saat tak ada anotasi. Sebelumnya initializer selalu menang, sehingga
        // `variabel r: bilangan = 5.7` bertipe Float dan konversi tak pernah
        // terjadi.
        if (node->type) {
            semanticType = semantic->context().getResolvedType(node->type);
            if (!semanticType && node->initializer) {
                semanticType = exprTypeOr(node->initializer, node->type);
            }
        } else if (node->initializer) {
            semanticType = exprTypeOr(node->initializer, node->type);
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
            // Konstruksi object dengan named-args tanpa initializer
            // (spec §15.3): variabel a: bilangan_kompleks(basis=...,
            // real="...", imajiner="...") -> call kx(real, imajiner).
            // Hanya object bilangan_kompleks; deklarasi lain tak tersentuh.
            if (node->type && node->type->kind == AstKind::CallTypeRepr) {
                const auto* ctr = static_cast<const CallTypeRepr*>(node->type);
                if (ctr->name == "bilangan_kompleks") {
                    ValueId rv = InvalidValue, iv = InvalidValue;
                    for (const auto* a : ctr->arguments) {
                        if (!a->value) continue;
                        ValueId* slotArg = nullptr;
                        if (a->name == "real")       slotArg = &rv;
                        else if (a->name == "imajiner") slotArg = &iv;
                        if (!slotArg) continue;
                        if (a->value->kind == AstKind::StringLiteralExpr) {
                            // value string di TypeRepr tak di-type-check
                            // semantic (getExprType = Error) -> bangun
                            // ConstString langsung dengan tipe teks.
                            const auto* sl =
                                static_cast<const StringLiteralExpr*>(a->value);
                            std::string decoded;
                            decodeStringLiteral(*sl, decoded);
                            TypeId tekst = mapType(
                                semantic->context().types().getBuiltinType(
                                    BuiltinTypeKind::Teks),
                                a->value);
                            ConstId cid = encodeStringLiteral(decoded, tekst, a->value);
                            *slotArg = builder.emit(
                                IrOpcode::ConstString, tekst,
                                {IrOperand::constant(cid)},
                                stmt->range.begin);
                        } else {
                            *slotArg = lowerExpr(a->value);
                        }
                    }
                    if (rv != InvalidValue && iv != InvalidValue) {
                        initial = emitCallByName("kx", {rv, iv},
                                                 stmt->range.begin);
                        // retype hasil ke varType (object instantiation) —
                        // mekanisme sama retype larik sized di atas.
                        if (initial != InvalidValue &&
                            varType != InvalidType) {
                            auto& f = builder.function(currentFunc);
                            auto& vv = f.values[initial];
                            if (vv.kind == IrValue::Kind::Instr &&
                                vv.payload < f.instrs.size()) {
                                f.instrs[vv.payload].resultType = varType;
                                vv.type = varType;
                            }
                        }
                    }
                }
            }
            // Default value eksplisit (zero/empty/construct), bukan
            // ConstInt 0 mentah — tipe non-integer butuh representasi
            // default yang sesuai (spesifikasi: verbose & eksplisit).
            if (initial == InvalidValue)
                initial = emitDefaultValue(varType, semanticType,
                                           stmt->range.begin);
            if (initial == InvalidValue && !failed) {
                return error(AstIrErrorCode::UnsupportedNode, stmt,
                             "default value tipe variabel belum didukung");
            }
        }
        if (builder.broken()) return false;
        // Konversi implisit numerik (spesifikasi: assignment ke variabel
        // ber-tipe menyisipkan cast bila tipe nilai berbeda — sebelumnya
        // nilai mengalir mentah, baru ketahuan saat RhBackend mengeksekusi).
        if (!failed && initial != InvalidValue && varType != InvalidType) {
            const TypeId initT =
                builder.function(currentFunc).values[initial].type;
            // Numeric ATAU Bool (int<->bool dikoerce; Bool = int 1-byte
            // di backend, narrowing path).
            const bool numA = initT != InvalidType &&
                              module.types.size() > initT &&
                              (module.types[initT].kind == IrTypeKind::Int ||
                               module.types[initT].kind == IrTypeKind::Float ||
                               module.types[initT].kind == IrTypeKind::Bool);
            const bool numB = module.types.size() > varType &&
                              (module.types[varType].kind == IrTypeKind::Int ||
                               module.types[varType].kind == IrTypeKind::Float ||
                               module.types[varType].kind == IrTypeKind::Bool);
            if (initT != InvalidType && initT != varType && numA && numB) {
                initial = builder.emit(IrOpcode::Cast, varType,
                                       {IrOperand::value(initial)},
                                       stmt->range.begin, node->name);
                if (builder.broken()) return false;
            }
        }
        // Larik hasil call builtin (ke_larik dll): registry menyatakan
        // return 'larik' unsized (View-sized, 2 unit) -> slot sempit,
        // elemen larik[N] menimpa slot tetangga. Annotasi larik[N] menang:
        // tulis tipe varType ke instr+value hasil SEBELUM backend mengalokasi
        // slot (mekanisme sama dengan foreach Opsi di LowerWhileStatement).
        if (!failed && initial != InvalidValue && varType != InvalidType &&
            module.types.size() > varType &&
            module.types[varType].kind == IrTypeKind::Array) {
            auto& fnMod = builder.function(currentFunc);
            auto& iv = fnMod.values[initial];
            if (iv.kind == IrValue::Kind::Instr && iv.payload < fnMod.instrs.size()) {
                const TypeId rt = fnMod.instrs[iv.payload].resultType;
                if (module.types.size() > rt &&
                    module.types[rt].kind == IrTypeKind::Array &&
                    module.types[rt].arraySize < module.types[varType].arraySize) {
                    fnMod.instrs[iv.payload].resultType = varType;
                    iv.type = varType;
                }
            }
        }
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
        // Nama bridge UNIK per goto: multi-goto ke label sama membuat
        // >1 bridge — nama identik membuat phi incoming tak bisa
        // membedakan pred (dominasi rusak, multi_goto.rhc gagal adopt).
        std::size_t nth = 1;
        for (const auto& pg0 : pendingGotos) {
            if (pg0.label == node->label) ++nth;
        }
        std::string bridgeName = "goto." + node->label;
        if (nth > 1) bridgeName += "." + std::to_string(nth);
        const BlockId bridge =
            builder.createBlock(currentFunc, bridgeName);
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                              {IrOperand::block(bridge)}, stmt->range.begin);
        PendingGoto pg;
        pg.bridge = bridge;
        pg.label = node->label;
        pg.loc = stmt->range.begin;
        // MERGE semua layer scope (outer -> inner, inner menang per
        // nama) — env.back() saja melewatkan assignment update-through
        // (layer phi) & var scope luar (snapshot then kosong utk var
        // label, fallback fnLocals -> def di blok yang tak dilewati).
        for (const auto& layer : env) {
            for (const auto& [n2, v2] : layer.vars) pg.env[n2] = v2;
        }
        pendingGotos.push_back(std::move(pg));
        setCurrent(InvalidBlock);
        return !builder.broken();
    }
    case AstKind::LabelStmt: {
        const auto* node = static_cast<const LabelStmt*>(stmt);
        // Deteksi GOTO MUNDUR ke label ini (loop via goto): scan body utk
        // GotoStmt dengan target == label. Jika ada, bangun phi carried
        // var di header label (pola sama loop lowering) supaya assignment
        // di body terbawa ke iterasi berikutnya — tanpa ini, binding SSA
        // statis memakai nilai pra-loop selamanya (goto.rhc infinite).
        bool hasBackGoto = false;
        std::function<void(const StmtNode*)> scanGoto;
        scanGoto = [&](const StmtNode* s) {
                if (!s) return;
                if (s->kind == AstKind::GotoStmt &&
                    static_cast<const GotoStmt*>(s)->label == node->label) {
                    hasBackGoto = true;
                    return;
                }
                if (s->kind == AstKind::CompoundStmt) {
                    for (const auto* c :
                         static_cast<const CompoundStmt*>(s)->statements)
                        scanGoto(c);
                } else if (s->kind == AstKind::IfStmt) {
                    const auto* i = static_cast<const IfStmt*>(s);
                    scanGoto(i->thenBranch);
                    scanGoto(i->elseBranch);
                }
            };
        scanGoto(node->statement);

        const BlockId preBlock = currentBlock;
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

        std::vector<std::pair<std::string, ValueId>> carried;
        if (hasBackGoto) {
            // Snapshot binding pra-label; phi per var, incoming pra-loop.
            // Semua use di body otomatis pakai phi (binding di-set phi).
            for (const auto& [name, val] : env.back().vars) {
                const auto& vv = builder.function(currentFunc).values[val];
                TypeId ty = vv.type;
                if (vv.kind == IrValue::Kind::Instr &&
                    vv.payload < builder.function(currentFunc).instrs.size()) {
                    ty = builder.function(currentFunc)
                             .instrs[vv.payload].resultType;
                }
                const ValueId phi =
                    builder.createPhi(ty, currentFunc, labelBlock, 0);
                builder.addPhiIncoming(currentFunc, phi, preBlock, val);
                env.back().vars[name] = phi;
                carried.emplace_back(name, phi);
            }
        }
        if (!lowerStmt(node->statement)) return false;
        if (hasBackGoto) {
            // Incoming back-edge: utk tiap pendingGoto ke label ini, pakai
            // binding akhir body (approx: nilai terakhir utk semua bridge).
            // Nilai back-edge = SNAPSHOT ENV SAAT GOTO (per bridge) —
            // benar utk goto sblm/ sesudah assignment manapun; fnLocals
            // terakhir bisa def di blok yg TIDAK dilewati edge goto (T2).
            const auto& fnL = builder.function(currentFunc).locals;
            for (const auto& pg : pendingGotos) {
                if (pg.label != node->label) continue;
                for (const auto& [name, phi] : carried) {
                    const auto itS = pg.env.find(name);
                    const ValueId v = (itS != pg.env.end())
                                          ? itS->second
                                          : (fnL.count(name)
                                                 ? fnL.at(name)
                                                 : InvalidValue);
                    if (v == InvalidValue) continue;
                    builder.addPhiIncoming(currentFunc, phi, pg.bridge, v);
                }
            }
            // Restore binding carried ke NILAI AKHIR BODY — uses
            // SESUDAH label membaca nilai exit (SSA: last write),
            // bukan phi (phi hanya live di dalam loop). Tanpa ini,
            // print setelah goto-loop membaca phi = nilai back-edge
            // terakhir (multi_goto.rhc: 4,4 padahal iterasi 5,5).
            for (const auto& [name, phi] : carried) {
                const auto itL = fnL.find(name);
                if (itL == fnL.end()) continue;
                env.back().vars[name] = itL->second;
            }
        }
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
