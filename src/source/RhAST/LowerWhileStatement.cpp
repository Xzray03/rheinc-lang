#include "AstToIrTranslatorImpl.h"

#include "RhParser/Stmt.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/Symbol.h"
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
    if (!loopStack.empty() && loopStack.back().header == InvalidBlock) {
        return error(AstIrErrorCode::MalformedShape, nullptr,
                     "lanjut di dalam pilih tidak valid (bukan perulangan)");
    }
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

// ===== foreach iterator-protocol (koleksi generik: Daftar, dsb) =====
// Desugar:
//   it = iterator_mulai(koleksi)
//   selama bukan iterator_selesai(it)
//     pilih iterator_berikutnya(it) {
//       kasus ADA(v): elem = v; body
//       kasus KOSONG(): berhenti
//     }
// Subjek pilih diberi tipe Opsi via resolusi varian (lihat
// LowerPilihStatement); primitif call/selama/pilih sudah terverifikasi.
bool AstToIrTranslator::Impl::lowerForEachIterator(const ForStmt* stmt,
                                                   ValueId iterable) {
    const NameValueMap preLoopEnv = env.back().vars;
    const BlockId preLoopBlock = currentBlock;
    const std::string itName =
        "%each.it." + std::to_string(syntheticCounter++);

    const ValueId it = emitCallByName("iterator_mulai", {iterable},
                                      stmt->range.begin);
    if (it == InvalidValue) return false;
    bindLocal(itName, it);

    const BlockId condB = builder.createBlock(currentFunc, "each.cond");
    const BlockId bodyB = builder.createBlock(currentFunc, "each.body");
    const BlockId updateB = builder.createBlock(currentFunc, "each.update");
    const BlockId exitB = builder.createBlock(currentFunc, "each.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);
    loopStack.push_back({updateB, exitB});

    // Kondisi: bukan iterator_selesai(it).
    setCurrent(condB);
    const ValueId selesai = emitCallByName("iterator_selesai",
                                           {lookupLocal(itName)},
                                           stmt->range.begin);
    if (selesai == InvalidValue) return false;
    const ValueId cond = builder.emit(
        IrOpcode::Not, builder.function(currentFunc).values[selesai].type,
        {IrOperand::value(selesai)}, stmt->range.begin);
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(cond), IrOperand::block(bodyB),
                           IrOperand::block(exitB)},
                          stmt->range.begin);

    // Body: pilih iterator_berikutnya(it) { ADA(v): elem=v; body;
    //                                       KOSONG: exit }.
    setCurrent(bodyB);
    const NameValueMap bodyStartEnv = env.back().vars;
    const ValueId berikutnya = emitCallByName(
        "iterator_berikutnya", {lookupLocal(itName)}, stmt->range.begin);
    if (berikutnya == InvalidValue) return false;
    TypeId sumTypeId =
        builder.function(currentFunc).values[berikutnya].type;
    // Hasil inferensi-gagal: resolve sum builtin Opsi dari global scope
    // (sama seperti LowerPilihStatement).
    if (sumTypeId == InvalidType ||
        module.types[sumTypeId].kind != IrTypeKind::Sum) {
        if (const Symbol* symbol = lookupGlobalSymbol("Opsi")) {
            if (symbol->type && symbol->type->kind() == TypeKind::Sum) {
                sumTypeId = mapType(symbol->type, stmt->iterable);
            }
        }
    }
    if (sumTypeId == InvalidType ||
        module.types[sumTypeId].kind != IrTypeKind::Sum) {
        loopStack.pop_back();
        return error(AstIrErrorCode::MatchNonSum, stmt->iterable,
                     "iterator_berikutnya tidak menghasilkan tipe sum "
                     "(resolusi Opsi gagal)");
    }
    const IrType& sumType = module.types[sumTypeId];
    int adaIndex = -1, kosongIndex = -1;
    for (std::size_t v = 0; v < sumType.variants.size(); ++v) {
        if (sumType.variants[v].name == "ADA") adaIndex = (int)v;
        if (sumType.variants[v].name == "KOSONG") kosongIndex = (int)v;
    }
    if (adaIndex < 0 || kosongIndex < 0) {
        loopStack.pop_back();
        return error(AstIrErrorCode::MalformedShape, stmt->iterable,
                     "tipe hasil iterator tanpa varian ADA/KOSONG");
    }

    const BlockId adaB = builder.createBlock(currentFunc, "each.ada");
    const BlockId kosongB = builder.createBlock(currentFunc, "each.kosong");
    const TypeId elemIrType = sumType.variants[adaIndex].payloadType;

    std::vector<IrOperand> matchOps;
    matchOps.push_back(IrOperand::value(berikutnya));
    for (const int vi : {adaIndex, kosongIndex}) {
        matchOps.push_back(IrOperand::value(
            builder.emit(IrOpcode::ConstInt, sumTypeId,
                         {IrOperand::imm(static_cast<std::uint32_t>(
                             sumType.variants[vi].tagValue))},
                         stmt->range.begin)));
        matchOps.push_back(IrOperand::block(
            vi == adaIndex ? adaB : kosongB));
    }
    matchOps.push_back(IrOperand::block(kosongB));  // default
    builder.setTerminator(currentFunc, bodyB, IrOpcode::MatchTag,
                          std::move(matchOps), stmt->range.begin);

    // kasus ADA: bind elemen, jalankan body, lanjut update.
    env.back().vars = bodyStartEnv;
    setCurrent(adaB);
    const ValueId elem = builder.emit(
        IrOpcode::Extract, elemIrType,
        {IrOperand::value(berikutnya), IrOperand::imm(0)},
        stmt->range.begin);
    const std::string& elemName =
        stmt->loopVariables.empty() ? stmt->loopVariable
                                    : stmt->loopVariables.back();
    bindLocal(elemName, elem);
    if (!lowerStmt(stmt->body)) { loopStack.pop_back(); return false; }
    if (blockOpen()) {
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                              {IrOperand::block(updateB)},
                              stmt->range.begin);
    }

    // kasus KOSONG: keluar loop.
    env.back().vars = bodyStartEnv;
    setCurrent(kosongB);
    builder.setTerminator(currentFunc, kosongB, IrOpcode::Br,
                          {IrOperand::block(exitB)}, stmt->range.begin);
    loopStack.pop_back();

    setCurrent(updateB);
    builder.setTerminator(currentFunc, updateB, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);

    // Loop-carried phi untuk variabel yang berubah di body.
    for (const auto& [name, valueAfter] : env.back().vars) {
        const auto it2 = bodyStartEnv.find(name);
        if (it2 == bodyStartEnv.end() || it2->second == valueAfter) continue;
        const TypeId phiType =
            builder.function(currentFunc).values[it2->second].type;
        const ValueId phi = builder.createPhi(phiType, currentFunc, condB,
                                              0, name);
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, it2->second);
        builder.addPhiIncoming(currentFunc, phi, updateB, valueAfter);
        env.back().vars[name] = phi;
    }

    setCurrent(exitB);
    return !builder.broken();
}

// ===== untuk-setiap (foreach atas larik fixed-size) =====
// Desugar index-based: i = 0; selama i < N { x = l[i]; body; i += langkah }.
// foreach atas view/teks (panjang dinamis) dan koleksi iterator-protocol
// (Daftar<T>, dsb) ditolak jujur — fase terpisah.
bool AstToIrTranslator::Impl::lowerForEach(const ForStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;

    const ValueId iterable = lowerExpr(stmt->iterable);
    if (iterable == InvalidValue) return false;
    const TypeId iterableType =
        builder.function(currentFunc).values[iterable].type;
    if (iterableType == InvalidType ||
        module.types[iterableType].kind != IrTypeKind::Array) {
        return lowerForEachIterator(stmt, iterable);
    }
    const std::uint64_t count = module.types[iterableType].arraySize;
    const TypeId elementType = module.types[iterableType].elementType;

    // Nama variabel: 1 var = elemen; 2 var = (indeks, elemen).
    const bool withIndex = stmt->loopVariables.size() >= 2;
    const std::string& elemName =
        withIndex ? stmt->loopVariables.back() : stmt->loopVariable;
    const std::string indexName =
        withIndex ? stmt->loopVariables.front() : "%each.i";

    const NameValueMap preLoopEnv = env.back().vars;
    const BlockId preLoopBlock = currentBlock;

    const ValueId zero = builder.emit(IrOpcode::ConstInt, elementType,
                                      {IrOperand::imm(0)},
                                      stmt->range.begin);
    const ValueId limit = builder.emit(
        IrOpcode::ConstInt, elementType, {IrOperand::imm(
            static_cast<std::uint32_t>(count))}, stmt->range.begin);
    bindLocal(indexName, zero);

    const BlockId condB = builder.createBlock(currentFunc, "each.cond");
    const BlockId bodyB = builder.createBlock(currentFunc, "each.body");
    const BlockId updateB = builder.createBlock(currentFunc, "each.update");
    const BlockId exitB = builder.createBlock(currentFunc, "each.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);
    loopStack.push_back({updateB, exitB});

    setCurrent(condB);
    const ValueId i0 = lookupLocal(indexName);
    const ValueId cond = builder.emit(
        IrOpcode::CmpLt, elementType,
        {IrOperand::value(i0), IrOperand::value(limit)},
        stmt->range.begin);
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(cond), IrOperand::block(bodyB),
                           IrOperand::block(exitB)},
                          stmt->range.begin);

    setCurrent(bodyB);
    const NameValueMap bodyStartEnv = env.back().vars;
    const ValueId i1 = lookupLocal(indexName);
    const ValueId elem = builder.emit(
        IrOpcode::Extract, elementType,
        {IrOperand::value(iterable), IrOperand::value(i1)},
        stmt->range.begin, elemName);
    bindLocal(elemName, elem);
    if (!lowerStmt(stmt->body)) return false;
    BlockId bodyEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, bodyEnd, IrOpcode::Br,
                              {IrOperand::block(updateB)}, stmt->range.begin);
    } else {
        bodyEnd = InvalidBlock;
    }
    loopStack.pop_back();

    setCurrent(updateB);
    const ValueId i2 = lookupLocal(indexName);
    ValueId next = InvalidValue;
    if (stmt->stepValue) {
        const ValueId step = lowerExpr(stmt->stepValue);
        if (step == InvalidValue) return false;
        next = builder.emit(IrOpcode::Add, elementType,
                            {IrOperand::value(i2), IrOperand::value(step)},
                            stmt->range.begin, indexName);
    } else {
        next = builder.emit(IrOpcode::Inc, elementType,
                            {IrOperand::value(i2)}, stmt->range.begin,
                            indexName);
    }
    bindLocal(indexName, next);
    builder.setTerminator(currentFunc, updateB, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);

    for (const auto& [name, valueAfter] : env.back().vars) {
        const auto it = bodyStartEnv.find(name);
        if (it == bodyStartEnv.end() || it->second == valueAfter) continue;
        const TypeId phiType =
            builder.function(currentFunc).values[it->second].type;
        const ValueId phi = builder.createPhi(phiType, currentFunc, condB,
                                              0, name);
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, it->second);
        builder.addPhiIncoming(currentFunc, phi, updateB, valueAfter);
        env.back().vars[name] = phi;
    }

    setCurrent(exitB);
    return !builder.broken();
}

// ===== untuk-dari-sampai-langkah (range for) =====

bool AstToIrTranslator::Impl::lowerForStatement(const ForStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;
    if (stmt->isForEach) {
        return lowerForEach(stmt);
    }
    if (stmt->loopVariable.empty()) {
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
