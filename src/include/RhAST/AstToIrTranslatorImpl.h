#pragma once

// Header INTERNAL (tidak di-install; hanya dipakai file .cpp RhAST).
// Mendefinisikan AstToIrTranslator::Impl — state translator fase 2 —
// agar konvensi "satu file sumber = satu fungsi" (spesifikasi Bagian
// 8.2) bisa dijaga: tiap Lower*.cpp/Map*.cpp mengimplementasikan tepat
// satu method Impl.

#include "RhAST/AstToIrTranslator.h"
#include "RhAST/IrBuilder.h"
#include "RhAST/IrErrorCatalog.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rhc {

class SemanticAnalyzer;
struct AstNode;
struct ExprNode;
struct FunctionDecl;
struct ParamDecl;
struct StmtNode;
struct StringLiteralExpr;
struct TypeReprNode;
struct TranslationUnitDecl;
class Symbol;
class Type;

struct AstToIrTranslator::Impl {
    Impl(SourceManager& sourceManagerRef, DiagnosticEngine& diagnosticsRef,
         AstToIrOptions optionsValue);

    SourceManager& sourceManager;
    DiagnosticEngine& diagnostics;
    AstToIrOptions options;

    IrModule module;
    IrBuilder builder;

    SemanticAnalyzer* semantic = nullptr;   // non-owning; prasyarat hidup
    const TranslationUnitDecl* unit = nullptr;
    bool failed = false;                    // hard-stop: error pertama
    bool moduleReleased = false;            // ownership IrModule sudah pindah

    // ===== Utilitas diagnostic =====
    // Error fase 2 (katalog); node boleh null (kembalikan false).
    bool error(AstIrErrorCode code, const AstNode* node,
               const std::string& detail);
    // Invariant internal (bug compiler): Fatal; builder jadi broken.
    bool fatal(const std::string& detail);

    // ===== Konteks fungsi & lingkungan (scope env) saat lowering =====
    FuncId currentFunc = InvalidFunc;
    BlockId currentBlock = InvalidBlock;   // block tempat instruksi disisipkan
    // Block "tutup" (setelah return/break/goto): statement berikutnya
    // di-skip sampai construct membuat block baru.
    bool blockOpen() const { return currentBlock != InvalidBlock; }

    // Lingkungan per scope: variabel (nama -> versi terkini) + antrian
    // jamin (defer) scope ini — FIFO sesuai DEVIATIONS.md D-1.
    struct ScopeEnv {
        std::unordered_map<std::string, ValueId> vars;
        std::vector<const StmtNode*> defers;   // body jamin, urutan FIFO
    };
    std::vector<ScopeEnv> env;

    // Konteks perulangan untuk berhenti/lanjut.
    struct LoopContext {
        BlockId header;   // lanjut -> br header
        BlockId exit;     // berhenti -> br exit
        // Situs lanjut: (block sumber, env vars saat lanjut) — dipakai
        // untuk melengkapi incoming phi loop-carried agar invariant
        // dominasi SSA terpenuhi (edge continue melewati rebind).
        std::vector<std::pair<BlockId, std::unordered_map<std::string, ValueId>>> continues;
    };
    std::vector<LoopContext> loopStack;

    // goto/label: resolusi dua arah (forward jump di-resolve setelah
    // semua block terbentuk — akhir lowerFunction).
    std::unordered_map<std::string, BlockId> labelBlocks;
    struct PendingGoto {
        BlockId bridge;      // block perantara yang menunggu terminator Br
        std::string label;
        SourceLocation loc;
    };
    std::vector<PendingGoto> pendingGotos;

    // Fungsi yang sudah terdaftar, agar panggilan ke fungsi yang
    // dideklarasikan lebih dulu/then belakang tetap resolve (forward ref).
    std::unordered_map<std::string, FuncId> functionsByName;

    // ===== Konstanta global (tetap) =====
    // By-value intern per modul: pembacaan men-emit ulang instruksi
    // konstanta di fungsi pemakai (dedup konstanta dilakukan oleh
    // constant pool untuk string; scalar inline). Global MUTABLE
    // (variabel/tugas_global) butuh global storage di IR — fase
    // terpisah (irVersion bump).
    struct GlobalConst {
        TypeId type = InvalidType;
        IrOpcode op = IrOpcode::ConstInt;   // ConstInt/ConstBool/ConstString
        std::uint64_t bits = 0;             // imm integer/bool
        ConstId poolId = InvalidConst;      // string
    };
    std::unordered_map<std::string, GlobalConst> globalConsts;
    bool lowerGlobalConst(const struct GlobalVarDecl* decl);  // LowerGlobal.cpp
    // Global mutable: nama -> indeks IrModule::globalVars; decls dengan
    // initializer non-literal dikerjakan fungsi implisit .__global_init.
    // Panggilan fungsi builtin TANPA node AST (untuk desugar): resolve
    // nama ke FuncId (stub bila perlu); tipe hasil dari symbol global
    // scope (fallback Void). Dipakai desugar foreach iterator-protocol.
    ValueId emitCallByName(const std::string& name,
                           std::vector<ValueId> args, SourceLocation loc);
    std::uint32_t syntheticCounter = 0;

    bool lowerGlobalMutable(const struct GlobalVarDecl* decl);  // LowerGlobal.cpp
    bool lowerGlobalInitFunction();                             // LowerGlobal.cpp
    std::vector<const struct GlobalVarDecl*> globalInitDecls;

    // ===== Model agregat (spesifikasi Bagian 3) =====
    // Aturan aliasability §3.1: variabel yang PERNAH di-alamati
    // (unary "alamat"/&) hidup memory-style (Alloc + Store/Load);
    // akses member-nya lewat FieldAddr — perubahan via pointer terlihat
    // di semua jalur baca. Variabel struct lain = SSA first-class
    // (Construct/Extract/Insert + versioning).
    std::unordered_set<std::string> addressTaken;   // di-reset per fungsi
    bool isMemoryStyle(const std::string& name) const {
        return addressTaken.find(name) != addressTaken.end();
    }
    // Nilai alamat (ptr Alloc) variabel memory-style; untuk variabel
    // biasa berarti versi SSA terkini (lihat lookupLocal/bindLocal).
    void collectAddressTaken(const StmtNode* stmt);  // CollectAddressTaken.cpp
    ValueId lowerMemberRead(const ExprNode* memberExpr);   // LowerMemberAccess.cpp
    ValueId lowerTernaryExpr(const struct TernaryExpr* expr);  // LowerTernaryExpr.cpp
    ValueId lowerSliceFrom(const ExprNode* objectExpr, const struct SliceExpr* node,
                           const ExprNode* locExpr);  // LowerExpr.cpp
    ValueId buildSliceView(ValueId source, const struct SliceExpr* node,
                           const ExprNode* locExpr);   // LowerExpr.cpp
    ValueId wrapSliceInOpsi(ValueId view, const ExprNode* locExpr);  // idem
    ValueId lowerSliceMulti(const ExprNode* objectExpr,
                            const struct ArrayLiteralExpr* slices,
                            const ExprNode* locExpr);   // idem
    FuncId lowerLambda(const struct LambdaExpr* expr);   // LowerLambda.cpp
    ValueId lowerSliceWrite(const struct ArrayIndexExpr* target,
                            const struct SliceExpr* slice,
                            const ExprNode* valueExpr);  // LowerExpr.cpp
    ValueId lowerArrayLiteral(const struct ArrayLiteralExpr* expr);   // LowerArray.cpp
    ValueId lowerArrayIndexRead(const struct ArrayIndexExpr* expr);   // LowerArray.cpp
    ValueId lowerArrayIndexWrite(const struct ArrayIndexExpr* target,
                                 const ExprNode* valueExpr);          // LowerArray.cpp
    ValueId lowerArrayIndexWriteValue(const struct ArrayIndexExpr* target, ValueId value);  // idem
    ValueId lowerMemberWrite(const ExprNode* target,
                             const ExprNode* valueExpr);   // LowerMemberAccess.cpp
    ValueId lowerMemberWriteValue(const ExprNode* target, ValueId value);  // idem (compound)
    // Default value (zero/empty) untuk tipe — dipakai VarDeclStmt tanpa
    // initializer dan elemen Construct default.
    ValueId emitDefaultValue(TypeId typeId, const Type* type,
                             SourceLocation loc);          // LowerMemberAccess.cpp

    // ===== Fase 2: lowering (satu fungsi per file) =====
    bool lowerTranslationUnit();                  // LowerTranslationUnit.cpp
    bool lowerFunction(const FunctionDecl* decl); // LowerFunction.cpp
    bool lowerStmt(const StmtNode* stmt);         // LowerStmt.cpp
    bool lowerIfStatement(const struct IfStmt* stmt);      // LowerIfStatement.cpp
    bool lowerWhileStatement(const struct WhileStmt* stmt); // LowerWhileStatement.cpp
    bool lowerDoWhileStatement(const struct DoWhileStmt* stmt);
    bool lowerForStatement(const struct ForStmt* stmt);
    bool lowerForEach(const struct ForStmt* stmt);  // LowerWhileStatement.cpp
    bool lowerForEachIterator(const struct ForStmt* stmt, ValueId iterable);  // idem
    bool lowerPilihStatement(const struct PilihStmt* stmt); // LowerPilihStatement.cpp
    bool lowerBreak();                            // LowerWhileStatement.cpp
    bool lowerContinue();
    void emitScopeDefers(std::size_t scopeIndex); // ExpandDefer.cpp
    void emitAllDefers();                         // ExpandDefer.cpp
    ValueId lowerExpr(const ExprNode* expr);      // LowerExpr.cpp (wrapper kontrak)
    ValueId lowerExprImpl(const ExprNode* expr);  // LowerExpr.cpp

    // node = konteks lokasi (hanya untuk pesan error).
    TypeId mapType(const Type* type, const AstNode* node = nullptr,
               const char* caller = nullptr);  // MapSemanticType.cpp
    ConstId encodeStringLiteral(const std::string& text, TypeId stringType,
                                const AstNode* node);  // EncodeConstant.cpp
    FuncId resolveCallee(const ExprNode* callee); // LowerExpr.cpp

    // Phi di join point (spesifikasi 4.2): gabungkan env pred-pred
    // pada join block; hasil diterapkan caller ke scope terkini.
    using NameValueMap = std::unordered_map<std::string, ValueId>;
    NameValueMap mergeAtJoin(
        BlockId join, const std::vector<std::pair<BlockId, const NameValueMap*>>& preds,
        const NameValueMap& fallback);            // InsertPhiAtJoin.cpp

    // SATU-SATUNYA cara mengubah block kerja: selalu sinkron dengan
    // insert point IrBuilder (builder.emit memakai insert point-nya
    // sendiri — duplikasi sumber kebenaran ini sumber bug).
    void setCurrent(BlockId block) {
        currentBlock = block;
        if (block != InvalidBlock && currentFunc != InvalidFunc) {
            builder.setInsertPoint(currentFunc, block);
        }
    }

    // Tipe ekspresi dengan fallback ke TypeRepr deklarasi: RhSemantic
    // kadang memberi ErrorType untuk node valid (mis. call generik
    // "alokasi(T)" — inferensi hasil tak terekam), sementara TypeRepr
    // penulisan carries tipe yang benar.
    const Type* exprTypeOr(const ExprNode* expr, const TypeReprNode* repr);

    // Symbol di global scope (builtin function/type teregister sebagai
    // symbol). Dipakai sebagai fallback tipe saat getExprType memberi
    // ErrorType tanpa diagnostic (temuan RhSemantic).
    const Symbol* lookupGlobalSymbol(const std::string& name) const;

    // true bila nama merujuk tipe struct di module (argumen generik
    // seperti "alokasi(Mahasiswa)" diparse sebagai IdentifierExpr).
    bool isStructTypeName(const std::string& name) const;
    const Type* lookupStructType(const std::string& name) const;
    TypeId pointerTo(TypeId elementType) {
        IrType t;
        t.kind = IrTypeKind::Pointer;
        t.elementType = elementType;
        t.sizeBytes = 8;
        t.alignBytes = 8;
        return builder.internType(t);
    }

    // helper kecil
    void pushEnv() { env.emplace_back(); }
    void popEnv() { env.pop_back(); }
    const NameValueMap& currentVars() const { return env.back().vars; }
    // Nilai tersimpan mentah: SSA value biasa, atau ptr Alloc untuk
    // variabel memory-style. Tidak meng-emit instruksi.
    ValueId lookupRaw(const std::string& name) const;
    // Lookup di env yang diberikan (untuk resolve capture lambda dari
    // scope pembuat setelah env di-swap).
    ValueId lookupRawIn(const std::vector<ScopeEnv>& envs,
                        const std::string& name) const {
        for (auto it = envs.rbegin(); it != envs.rend(); ++it) {
            const auto found = it->vars.find(name);
            if (found != it->vars.end()) return found->second;
        }
        return InvalidValue;
    }
    // Nilai untuk dibaca: variabel memory-style di-Load dulu.
    ValueId lookupLocal(const std::string& name);
    void bindLocal(const std::string& name, ValueId value);
};

// true bila tipe mengandung ErrorType di kedalaman (definisi di
// AstToIrTranslator.cpp) — lihat komentar di sana untuk rasional.
bool typeContainsError(const Type* type);

// Walker identifier (definisi di CollectAddressTaken.cpp) — untuk
// deteksi capture lambda.
void collectIdentifiers(const ExprNode* expr,
                        std::vector<std::string>& out);
void collectStmtIdentifiers(const StmtNode* stmt,
                            std::vector<std::string>& out);

} // namespace rhc
