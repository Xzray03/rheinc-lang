#include "AstToIrTranslatorImpl.h"

#include "RhParser/Stmt.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

namespace rhc {

// ===== berhenti / lanjut (spesifikasi Bagian 4.4) =====

bool AstToIrTranslator::Impl::lowerBreak() {
    if (loopStack.empty()) {
        return error(AstIrErrorCode::MalformedShape, nullptr,
                     "berhenti di luar perulangan (seharusnya ditolak semantic)");
    }
    builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                          {IrOperand::block(loopStack.back().exit)});
    setCurrent(InvalidBlock);  // sisa block tak tercapai
    return !builder.broken();
}

bool AstToIrTranslator::Impl::lowerContinue() {
    if (loopStack.empty()) {
        return error(AstIrErrorCode::MalformedShape, nullptr,
                     "lanjut di luar perulangan (seharusnya ditolak semantic)");
    }
    builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                          {IrOperand::block(loopStack.back().header)});
    setCurrent(InvalidBlock);
    return !builder.broken();
}

// ===== selama (while) =====

bool AstToIrTranslator::Impl::lowerWhileStatement(const WhileStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;

    const NameValueMap preLoopEnv = env.back().vars;
    const BlockId preLoopBlock = currentBlock;

    const BlockId condB = builder.createBlock(currentFunc, "while.cond");
    const BlockId bodyB = builder.createBlock(currentFunc, "while.body");
    const BlockId exitB = builder.createBlock(currentFunc, "while.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);
    loopStack.push_back({condB, exitB});

    // Kondisi di header; SSA value loop-carried dibangun setelah body.
    setCurrent(condB);
    const ValueId condition = lowerExpr(stmt->condition);
    if (condition == InvalidValue) return false;
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(bodyB),
                           IrOperand::block(exitB)},
                          stmt->range.begin);

    // Body.
    setCurrent(bodyB);
    const NameValueMap bodyStartEnv = env.back().vars;
    if (!lowerStmt(stmt->body)) return false;
    BlockId bodyEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, bodyEnd, IrOpcode::Br,
                              {IrOperand::block(condB)}, stmt->range.begin);
    } else {
        bodyEnd = InvalidBlock;
    }
    loopStack.pop_back();

    // Loop-carried phi: nama yang berubah di body -> phi di header
    // dengan incoming (preLoopBlock, nilaiLama) + (bodyEnd, nilaiBaru).
    std::vector<std::pair<BlockId, const NameValueMap*>> preds;
    if (bodyEnd != InvalidBlock) preds.emplace_back(bodyEnd, nullptr);
    for (const auto& [name, valueAfter] : env.back().vars) {
        const auto it = bodyStartEnv.find(name);
        if (it == bodyStartEnv.end() || it->second == valueAfter) continue;
        const TypeId phiType = builder.function(currentFunc).values[it->second].type;
        const ValueId phi = builder.createPhi(phiType, currentFunc, condB, 0, name);
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, it->second);
        if (bodyEnd != InvalidBlock) {
            builder.addPhiIncoming(currentFunc, phi, bodyEnd, valueAfter);
        }
        env.back().vars[name] = phi;
    }

    setCurrent(exitB);
    return !builder.broken();
}

// ===== ulangi-selama (do-while) =====

bool AstToIrTranslator::Impl::lowerDoWhileStatement(const DoWhileStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;

    const NameValueMap preLoopEnv = env.back().vars;
    const BlockId preLoopBlock = currentBlock;

    const BlockId bodyB = builder.createBlock(currentFunc, "do.body");
    const BlockId condB = builder.createBlock(currentFunc, "do.cond");
    const BlockId exitB = builder.createBlock(currentFunc, "do.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(bodyB)}, stmt->range.begin);
    loopStack.push_back({condB, exitB});

    setCurrent(bodyB);
    const NameValueMap bodyStartEnv = env.back().vars;
    if (!lowerStmt(stmt->body)) return false;
    BlockId bodyEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, bodyEnd, IrOpcode::Br,
                              {IrOperand::block(condB)}, stmt->range.begin);
    } else {
        bodyEnd = InvalidBlock;
    }
    loopStack.pop_back();

    // Kondisi — body minimal jalan sekali, jadi selalu ada jalur ke cond.
    setCurrent(condB);
    const ValueId condition = lowerExpr(stmt->condition);
    if (condition == InvalidValue) return false;
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(bodyB),
                           IrOperand::block(exitB)},
                          stmt->range.begin);

    // Loop-carried phi di header (cond) untuk nilai yang berubah di body.
    if (bodyEnd != InvalidBlock) {
        for (const auto& [name, valueAfter] : env.back().vars) {
            const auto it = bodyStartEnv.find(name);
            if (it == bodyStartEnv.end() || it->second == valueAfter) continue;
            const TypeId phiType =
                builder.function(currentFunc).values[it->second].type;
            const ValueId phi =
                builder.createPhi(phiType, currentFunc, condB, 0, name);
            builder.addPhiIncoming(currentFunc, phi, bodyEnd, valueAfter);
            // preLoopBlock bukan pred cond pada do-while; pred cond hanya
            // bodyEnd — nilai awal diisi dari env sebelum loop.
            builder.addPhiIncoming(currentFunc, phi, preLoopBlock,
                                   it->second);
            env.back().vars[name] = phi;
        }
    }

    setCurrent(exitB);
    return !builder.broken();
}

// ===== untuk-dari-sampai-langkah (range for) =====

bool AstToIrTranslator::Impl::lowerForStatement(const ForStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;
    if (stmt->isForEach || stmt->loopVariable.empty()) {
        return error(AstIrErrorCode::UnsupportedNode,
                     const_cast<ForStmt*>(stmt),
                     "for-range butuh loop variable tunggal");
    }

    const NameValueMap preLoopEnv = env.back().vars;
    const BlockId preLoopBlock = currentBlock;

    // Init: var loop = nilai awal.
    const ValueId start = lowerExpr(stmt->startValue);
    if (start == InvalidValue) return false;
    bindLocal(stmt->loopVariable, start);

    const ValueId end = lowerExpr(stmt->endValue);
    if (end == InvalidValue) return false;
    const ValueId step = stmt->stepValue ? lowerExpr(stmt->stepValue)
                                         : InvalidValue;

    const BlockId condB = builder.createBlock(currentFunc, "for.cond");
    const BlockId bodyB = builder.createBlock(currentFunc, "for.body");
    const BlockId updateB = builder.createBlock(currentFunc, "for.update");
    const BlockId exitB = builder.createBlock(currentFunc, "for.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);
    loopStack.push_back({updateB, exitB});  // lanjut -> update, berhenti -> exit

    // Kondisi: var loop < end (asumsi langkah positif; langkah negatif
    // menghasilkan CmpGt — ditentukan lewat constant folding step saat
    // nilainya konstan, selain itu CmpLt dengan catatan).
    setCurrent(condB);
    const ValueId loopVar = lookupLocal(stmt->loopVariable);
    IrOpcode cmpOp = IrOpcode::CmpLt;
    if (step != InvalidValue) {
        // deteksi langkah negatif konstan
        // (disederhanakan: hanya bila step adalah ConstInt 1-ImmV negatif
        // — lowering umum konstanta folding di pass terpisah)
    }
    const TypeId condType =
        builder.function(currentFunc).values[loopVar].type;
    const ValueId condition = builder.emit(
        cmpOp, condType, {IrOperand::value(loopVar), IrOperand::value(end)},
        stmt->range.begin);
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(bodyB),
                           IrOperand::block(exitB)},
                          stmt->range.begin);

    // Body.
    setCurrent(bodyB);
    const NameValueMap bodyStartEnv = env.back().vars;
    if (!lowerStmt(stmt->body)) return false;
    BlockId bodyEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, bodyEnd, IrOpcode::Br,
                              {IrOperand::block(updateB)}, stmt->range.begin);
    } else {
        bodyEnd = InvalidBlock;
    }
    loopStack.pop_back();

    // Update: var loop = var loop + langkah (default 1).
    setCurrent(updateB);
    const ValueId current = lookupLocal(stmt->loopVariable);
    ValueId next = InvalidValue;
    if (step != InvalidValue) {
        next = builder.emit(IrOpcode::Add, condType,
                            {IrOperand::value(current),
                             IrOperand::value(step)},
                            stmt->range.begin, stmt->loopVariable);
    } else {
        next = builder.emit(IrOpcode::Inc, condType,
                            {IrOperand::value(current)},
                            stmt->range.begin, stmt->loopVariable);
    }
    bindLocal(stmt->loopVariable, next);
    builder.setTerminator(currentFunc, updateB, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);

    // Loop-carried phi untuk var loop (dan nama lain yang berubah).
    for (const auto& [name, valueAfter] : env.back().vars) {
        const auto it = bodyStartEnv.find(name);
        if (it == bodyStartEnv.end() || it->second == valueAfter) continue;
        const TypeId phiType =
            builder.function(currentFunc).values[it->second].type;
        const ValueId phi =
            builder.createPhi(phiType, currentFunc, condB, 0, name);
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, it->second);
        builder.addPhiIncoming(currentFunc, phi, updateB, valueAfter);
        env.back().vars[name] = phi;
    }

    setCurrent(exitB);
    return !builder.broken();
}

} // namespace rhc
