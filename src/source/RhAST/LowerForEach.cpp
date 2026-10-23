#include "RhAST/AstToIrTranslatorImpl.h"
#include "RhIR/IrDomAnalysis.h"

#include "RhParser/Stmt.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

namespace rhc {

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
    loopStack.push_back({updateB, exitB, {}});

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
    // Registry iterator_berikutnya tak membawa tipe hasil (fallback Void),
    // padahal backend membaca layout Sum dari instr.resultType call — tulis
    // tipe Opsi ter-resolve ke instruksi call agar Extract payload benar.
    {
        auto& fnMod = builder.function(currentFunc);
        auto& bv = fnMod.values[berikutnya];
        if (bv.kind == IrValue::Kind::Instr && bv.payload < fnMod.instrs.size()) {
            fnMod.instrs[bv.payload].resultType = sumTypeId;
            bv.type = sumTypeId;   // verifikasi: value.type == resultType instr
        }
    }
    const IrType sumType = module.types[sumTypeId];  // by value: anti-UAF realloc
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
        // Nama phi HARUS unik per fungsi: dua loop `untuk i` berturut-turut
        // menyebabkan collision nama `%i` -> adopt remap salah, phi move
        // membaca home loop-1 -> loop kedua beku (rekursif.rhc).
        std::string phiName = name;
        {
            // Cek collision thd SEMUA nama value di fungsi ini (locals
            // saja tak cukup — loop var `untuk i` tak masuk locals, maka
            // collision `%i` antar-dua-loop tetap terjadi & lp5 hang).
            const auto& vv = builder.function(currentFunc).values;
            auto nameTaken = [&](const std::string& nm) {
                for (const auto& v : vv) {
                    if (v.name == nm) return true;
                }
                return builder.function(currentFunc).locals.count(nm) > 0;
            };
            if (nameTaken(phiName)) {
                int n = 2;
                while (nameTaken(phiName + "_" + std::to_string(n))) {
                    ++n;
                }
                phiName += "_" + std::to_string(n);
            }
        }
        const ValueId phi = builder.createPhi(phiType, currentFunc, condB,
                                              0, phiName);
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, it2->second);
        // Edge lanjut menarget updateB dan melewati rebind: nilai latch
        // wajib merge phi di updateB (pred updateB yang bukan situs
        // continue + tiap situs continue) agar value mendominasi edge
        // updateB -> condB (invariant dominasi SSA). Guard InvalidBlock.
        ValueId latchValue = valueAfter;
        bool latchOk = false;  // continue-latch phi ditunda: nilai env saat
                          // lanjut belum dominasi-teruji (audit dataflow);
                          // wiring lama + phi exit (mergeVars) tetap berlaku.
        for (const auto& cs : loopStack.back().continues) {
            if (cs.first == InvalidBlock) latchOk = false;
        }
        if (latchOk) {
            // Nama phi HARUS unik per fungsi (dua foreach `setiap x`
            // berturut -> collision -> remap salah). Pemisah "%" dipertahankan.
            std::string mgName = name + "%latch";
            {
                const auto& vv = builder.function(currentFunc).values;
                auto taken = [&](const std::string& nm) {
                    for (const auto& v : vv) {
                        if (v.name == nm) return true;
                    }
                    return builder.function(currentFunc).locals.count(nm) > 0;
                };
                if (taken(mgName)) {
                    int n = 2;
                    while (taken(mgName + "_" + std::to_string(n))) ++n;
                    mgName += "_" + std::to_string(n);
                }
            }
            const ValueId mergePhi = builder.createPhi(
                phiType, currentFunc, updateB, 0, mgName);
            for (const BlockId p :
                 builder.function(currentFunc).blocks[updateB].preds) {
                bool isCs = false;
                for (const auto& cs : loopStack.back().continues) {
                    if (cs.first == p) { isCs = true; break; }
                }
                if (!isCs) {
                    builder.addPhiIncoming(currentFunc, mergePhi, p,
                                           valueAfter);
                }
            }
            // Nilai incoming per situs continue HANYA dipakai bila def-
            // nya mendominasi block situs (uji dominasi nyata, bukan
            // asumsi leksikal); jika tidak, fallback nilai pra-loop yang
            // selalu dominan. Ini menjaga invariant SSA ketat.
            IrFunction& fnR = builder.function(currentFunc);
            std::vector<std::int32_t> defBlk(fnR.instrs.size(), -1);
            for (BlockId db = 0; db < fnR.blocks.size(); ++db) {
                for (const InstrId id : fnR.blocks[db].insts) {
                    if (id < fnR.instrs.size()) defBlk[id] = db;
                }
            }
            const DomInfo domR = computeDom(fnR);
            const auto dominatesBlk = [&](ValueId v, BlockId at) -> bool {
                if (v >= fnR.values.size()) return false;
                const IrValue& vv = fnR.values[v];
                if (vv.kind != IrValue::Kind::Instr) return true;  // Param
                if (vv.payload >= fnR.instrs.size()) return false;
                const std::int32_t db = defBlk[vv.payload];
                if (db < 0) return false;
                return domR.dominates(at, static_cast<std::uint32_t>(db));
            };
            for (const auto& cs : loopStack.back().continues) {
                const auto cv = cs.second.find(name);
                ValueId csVal =
                    cv != cs.second.end() ? cv->second : it2->second;
                if (!dominatesBlk(csVal, cs.first)) csVal = it2->second;
                builder.addPhiIncoming(currentFunc, mergePhi, cs.first,
                                       csVal);
            }
            latchValue = mergePhi;
        }
        builder.addPhiIncoming(currentFunc, phi, updateB, latchValue);
        env.back().vars[name] = phi;
    }

    // Variabel elemen bersifat scoped terhadap loop: nilainya didefinisikan
    // di body yang TIDAK mendominasi exitB (jalur kosong: cond -> exit),
    // sehingga tidak boleh bocor ke env pasca-loop (invariant dominasi SSA,
    // DefDoesNotDominateUse). Hapus dari env saat keluar loop.
    env.back().vars.erase(elemName);

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
    // View: panjang dinamis — dihitung via Len saat runtime.
    const bool isView = iterableType != InvalidType &&
                        module.types[iterableType].kind == IrTypeKind::View;
    if ((iterableType == InvalidType ||
         (module.types[iterableType].kind != IrTypeKind::Array && !isView))) {
        return lowerForEachIterator(stmt, iterable);
    }
    const TypeId elementType = module.types[iterableType].elementType;
    const ValueId dynamicLimit =
        isView ? builder.emit(IrOpcode::Len, elementType,
                              {IrOperand::value(iterable)},
                              stmt->range.begin)
               : InvalidValue;
    const std::uint64_t count =
        isView ? 0 : module.types[iterableType].arraySize;

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
    const ValueId limit =
        isView ? dynamicLimit
               : builder.emit(IrOpcode::ConstInt, elementType,
                              {IrOperand::imm(static_cast<std::uint32_t>(
                                  count))},
                              stmt->range.begin);
    bindLocal(indexName, zero);

    const BlockId condB = builder.createBlock(currentFunc, "each.cond");
    const BlockId bodyB = builder.createBlock(currentFunc, "each.body");
    const BlockId updateB = builder.createBlock(currentFunc, "each.update");
    const BlockId exitB = builder.createBlock(currentFunc, "each.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);
    loopStack.push_back({updateB, exitB, {}});

    // Phi PRE-CREATED di header condB untuk index iterator + seluruh
    // variabel env pra-loop (pola untuk/while/do-while): cond & body
    // membaca phi, bukan nilai pra-loop.
    setCurrent(condB);
    NameValueMap preLoopEnvFE = env.back().vars;
    preLoopEnvFE[indexName] = lookupLocal(indexName);
    std::vector<std::pair<std::string, ValueId>> carriedFE;
    for (const auto& [name, oldValue] : preLoopEnvFE) {
        const TypeId phiType =
            builder.function(currentFunc).values[oldValue].type;
        const ValueId phi = builder.createPhi(phiType, currentFunc, condB, 0,
                                              name);
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, oldValue);
        env.back().vars[name] = phi;
        carriedFE.emplace_back(name, phi);
    }
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
    const bool savedCapture = capturePoppedEnv;
    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { capturePoppedEnv = savedCapture; return false; }
    capturePoppedEnv = savedCapture;
    BlockId bodyEnd = currentBlock;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, bodyEnd, IrOpcode::Br,
                              {IrOperand::block(updateB)}, stmt->range.begin);
    } else {
        bodyEnd = InvalidBlock;
    }

    const auto continuesFE = loopStack.back().continues;
    loopStack.pop_back();

    setCurrent(updateB);
    // Phi merge latch khusus loop var di updateB
    // masuk updateB dari akhir body / situs lanjut.
    ValueId latchIdx = InvalidValue;
    {
        const auto envEnd = env.back().vars.find(indexName);
        const auto itC = carriedFE.end();
        (void)itC;
        if (envEnd != env.back().vars.end()) {
            // Nama phi HARUS unik per fungsi (dua foreach `setiap i, x`
            // berturut -> collision `i_latch` -> remap salah).
            std::string liName = indexName + "_latch";
            {
                const auto& vv = builder.function(currentFunc).values;
                auto taken = [&](const std::string& nm) {
                    for (const auto& v : vv) {
                        if (v.name == nm) return true;
                    }
                    return builder.function(currentFunc).locals.count(nm) > 0;
                };
                if (taken(liName)) {
                    int n = 2;
                    while (taken(liName + "_" + std::to_string(n))) ++n;
                    liName += "_" + std::to_string(n);
                }
            }
            latchIdx = builder.createPhi(elementType, currentFunc, updateB, 0,
                                         liName);
            // incoming: dari akhir body & continue-sites. Merge SEMUA
            // layer scope — poppedEnv saja (scope terakhir) melewatkan
            // indexName (scope foreach, bukan body) -> latch phi kosong
            // -> inc dari garbage -> 1 iter saja (foreach sugar).
            NameValueMap postBody = poppedEnv;
            for (const auto& layer : env) {
                for (const auto& [n2, v2] : layer.vars) {
                    if (!postBody.count(n2)) postBody[n2] = v2;
                }
            }
            for (const auto& [pred, envMap] : continuesFE) {
                const auto cv = envMap.find(indexName);
                if (cv != envMap.end()) {
                    builder.addPhiIncoming(currentFunc, latchIdx, pred,
                                           cv->second);
                }
            }
            // bodyEnd pred -> postBody[indexName] (phi condB bila tak berubah)
            const auto pb = postBody.find(indexName);
            if (pb != postBody.end()) {
                // bodyEnd = pred terakhir; cari dari preds updateB
                const IrFunction& f = builder.function(currentFunc);
                for (const BlockId pred : f.blocks[updateB].preds) {
                    bool found = false;
                    for (const auto& [cp, cm] : continuesFE) {
                        if (cp == pred) { found = true; break; }
                    }
                    if (!found) {
                        builder.addPhiIncoming(currentFunc, latchIdx, pred,
                                               pb->second);
                    }
                }
            }
        }
    }
    const ValueId i2 = latchIdx != InvalidValue ? latchIdx
                                                : lookupLocal(indexName);
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

    // Wiring incoming phi condB (carriedFE): nama yang berubah mendapat
    // incoming dari updateB = nilai env akhir (valueAfter); nama yang
    // sama -> self-incoming (benign, removeable).
    // Nilai back-edge: MERGE poppedEnv (scope terakhir) + function
    // locals — poppedEnv saja melewatkan index foreach (scope terpisah)
    // -> phi index tanpa incoming back-edge (each.i tak advance,
    // foreach print element pertama berulang, iterator.rhc hang).
    const NameValueMap& fnLc = builder.function(currentFunc).locals;
    for (const auto& [name, phi] : carriedFE) {
        const auto itAfter = poppedEnv.find(name);
        ValueId v = InvalidValue;
        if (itAfter != poppedEnv.end()) {
            v = itAfter->second;
        } else {
            const auto itL = fnLc.find(name);
            if (itL == fnLc.end()) continue;
            v = itL->second;
        }
        if (v == phi) continue;  // tak berubah -> self
        builder.addPhiIncoming(currentFunc, phi, updateB, v);
    }
    // Nilai keluar loop = EXIT PHI (lihat lowerWhileStatement): value di
    // exit block mendominasi semua pemakaian setelah loop.
    for (const auto& [name, phi] : carriedFE) {
        const TypeId outT = builder.function(currentFunc).values[phi].type;
        const ValueId out = builder.createPhi(outT, currentFunc, exitB, 0, name);
        builder.addPhiIncoming(currentFunc, out, condB, phi);
        env.back().vars[name] = out;
        builder.function(currentFunc).locals[name] = out;
    }

    // Variabel elemen bersifat scoped terhadap loop: nilainya didefinisikan
    // di body yang TIDAK mendominasi exitB (jalur kosong: cond -> exit),
    // sehingga tidak boleh bocor ke env pasca-loop (invariant dominasi SSA,
    // DefDoesNotDominateUse). Hapus dari env saat keluar loop.
    env.back().vars.erase(elemName);

    setCurrent(exitB);
    return !builder.broken();
}

} // namespace rhc
