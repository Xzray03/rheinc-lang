#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Stmt.h"

namespace rhc {

// Ekspansi jamin (defer) — FIFO per scope, sesuai DEVIATIONS.md D-1.
//
// Saat scope keluar secara normal (CompoundStmt popEnv), body jamin
// scope tersebut disisipkan di block saat ini SEBELUM popEnv.
// Saat kembalikan, SEMUA jamin aktif (semua scope) disisipkan LIFO
// scope-wise (scope dalam dulu), FIFO per scope.
//
// Catatan: berhenti/lanjut yang melompat keluar scope tidak memicu
// jamin scope yang di-lewati — ini batasan sementara (perilaku
// eksplisit belum didefinisikan di corpus).

void AstToIrTranslator::Impl::emitScopeDefers(std::size_t scopeIndex) {
    if (failed || builder.broken() || !blockOpen()) return;
    const ScopeEnv& scope = env[scopeIndex];
    for (const StmtNode* body : scope.defers) {
        if (!lowerStmt(body)) return;
    }
}

void AstToIrTranslator::Impl::emitAllDefers() {
    if (failed || builder.broken() || !blockOpen()) return;
    for (std::size_t i = env.size(); i-- > 0; ) {
        emitScopeDefers(i);
        if (failed || builder.broken()) return;
    }
}

} // namespace rhc
