#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Stmt.h"

namespace rhc {

// Lowering jika/selain-itu (spesifikasi Bagian 4):
//   condBr kond, then, else; join menerima phi untuk variabel yang
//   berbeda nilai antar cabang. Cabang yang berakhir dengan terminator
//   (kembalikan) tidak menjadi pred join.
bool AstToIrTranslator::Impl::lowerIfStatement(const IfStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;

    // Snapshot env.back() asli untuk restore antar cabang; basis merge
    // = flattenEnv() (SEMUA layer) agar variabel scope luar yang
    // di-assign di dalam cabang ikut ter-merge lewat phi join — tanpa
    // ini nilai baru terjebak di scope cabang yang pop (loop while
    // infinite; do-while kondisi membaca value yang tak mendominasi).
    const NameValueMap preBackVars = env.back().vars;
    const NameValueMap preEnv = flattenEnv();

    // Kondisi dievaluasi di block SAAT INI (sebelum cabang dibuat).
    const ValueId condition = lowerExpr(stmt->condition);
    if (condition == InvalidValue) return false;

    const BlockId condBlock = currentBlock;
    const BlockId thenB = builder.createBlock(currentFunc, "then");
    const BlockId elseB = builder.createBlock(currentFunc, "else");
    const BlockId joinB = builder.createBlock(currentFunc, "join");
    builder.setTerminator(currentFunc, condBlock, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(thenB),
                           IrOperand::block(elseB)},
                          stmt->range.begin);

    // Cabang then. Assignment ke variabel luar menulis scope terdalam
    // (assignLocal), yang hilang saat body (compound) pop — capture lewat
    // capturePoppedEnv, lalu gabung ke snapshot pra-cabang. Nama yang
    // DIPANGGIL ulang di cabang (shadow) tidak menembus join.
    setCurrent(thenB);
    const bool savedCaptureThen = capturePoppedEnv;
    capturePoppedEnv = true; poppedEnv.clear(); poppedDeclared.clear();
    if (!lowerStmt(stmt->thenBranch)) { capturePoppedEnv = savedCaptureThen; return false; }
    capturePoppedEnv = savedCaptureThen;
    NameValueMap thenEnv = preEnv;
    for (const auto& [name, v] : poppedEnv) {
        if (!preEnv.count(name)) continue;      // lahir di cabang: tak hidup di join
        if (poppedDeclared.count(name)) continue;  // shadow: mati saat pop
        thenEnv[name] = v;
    }
    // Cabang tanpa compound (statement tunggal): update langsung ke
    // env.back() — ambil diff-nya (no-op bila compound sudah restore).
    for (const auto& [name, v] : env.back().vars) {
        const auto it = preEnv.find(name);
        if (it != preEnv.end() && it->second != v) thenEnv[name] = v;
    }
    BlockId thenEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, thenEnd, IrOpcode::Br,
                              {IrOperand::block(joinB)}, stmt->range.begin);
    } else {
        thenEnd = InvalidBlock;  // cabang berakhir terminator (kembalikan)
    }

    // Cabang else — mulai dari env yang sama dengan sebelum jika.
    env.back().vars = preBackVars;
    setCurrent(elseB);
    const bool savedCaptureElse = capturePoppedEnv;
    capturePoppedEnv = true; poppedEnv.clear(); poppedDeclared.clear();
    if (stmt->elseBranch) {
        if (!lowerStmt(stmt->elseBranch)) { capturePoppedEnv = savedCaptureElse; return false; }
    }
    capturePoppedEnv = savedCaptureElse;
    NameValueMap elseEnv = preEnv;
    for (const auto& [name, v] : poppedEnv) {
        if (!preEnv.count(name)) continue;
        if (poppedDeclared.count(name)) continue;
        elseEnv[name] = v;
    }
    for (const auto& [name, v] : env.back().vars) {
        const auto it = preEnv.find(name);
        if (it != preEnv.end() && it->second != v) elseEnv[name] = v;
    }
    BlockId elseEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, elseEnd, IrOpcode::Br,
                              {IrOperand::block(joinB)}, stmt->range.begin);
    } else {
        elseEnd = InvalidBlock;
    }

    // Gabung di join dengan phi bila perlu.
    std::vector<std::pair<BlockId, const NameValueMap*>> preds;
    if (thenEnd != InvalidBlock) preds.emplace_back(thenEnd, &thenEnv);
    if (elseEnd != InvalidBlock) preds.emplace_back(elseEnd, &elseEnv);

    if (preds.empty()) {
        // Kedua cabang berakhir — join tak tercapai.
        builder.setTerminator(currentFunc, joinB, IrOpcode::Unreachable, {},
                              stmt->range.begin);
        env.back().vars = preBackVars;
        setCurrent(InvalidBlock);  // join tak tercapai: skip sampai block baru
        return !builder.broken();
    }

    env.back().vars = mergeAtJoin(joinB, preds, preEnv);
    setCurrent(joinB);
    return !builder.broken();
}

} // namespace rhc
