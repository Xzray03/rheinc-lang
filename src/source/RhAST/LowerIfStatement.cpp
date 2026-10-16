#include "AstToIrTranslatorImpl.h"

#include "RhParser/Stmt.h"

namespace rhc {

// Lowering jika/selain-itu (spesifikasi Bagian 4):
//   condBr kond, then, else; join menerima phi untuk variabel yang
//   berbeda nilai antar cabang. Cabang yang berakhir dengan terminator
//   (kembalikan) tidak menjadi pred join.
bool AstToIrTranslator::Impl::lowerIfStatement(const IfStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;

    const NameValueMap preEnv = env.back().vars;

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

    // Cabang then.
    setCurrent(thenB);
    if (!lowerStmt(stmt->thenBranch)) return false;
    const NameValueMap thenEnv = env.back().vars;
    BlockId thenEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, thenEnd, IrOpcode::Br,
                              {IrOperand::block(joinB)}, stmt->range.begin);
    } else {
        thenEnd = InvalidBlock;  // cabang berakhir terminator (kembalikan)
    }

    // Cabang else — mulai dari env yang sama dengan sebelum jika.
    env.back().vars = preEnv;
    setCurrent(elseB);
    if (stmt->elseBranch) {
        if (!lowerStmt(stmt->elseBranch)) return false;
    }
    const NameValueMap elseEnv = env.back().vars;
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
        env.back().vars = preEnv;
        setCurrent(InvalidBlock);  // join tak tercapai: skip sampai block baru
        return !builder.broken();
    }

    env.back().vars = mergeAtJoin(joinB, preds, preEnv);
    setCurrent(joinB);
    return !builder.broken();
}

} // namespace rhc
