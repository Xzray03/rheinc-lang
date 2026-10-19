#include "RhAST/AstToIrTranslatorImpl.h"
#include "RhIR/IrDomAnalysis.h"

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
    // Rekam situs lanjut (block sumber + env) sebelum menutup block:
    // edge continue adalah pred tambahan dari header, dan phi loop-
    // carried wajib punya incoming untuk edge ini.
    // Snapshot MERGE semua layer scope (back menang per nama). env.back()
    // saja melewatkan assignment update-through — assignLocal register-
    // style menulis ke LAYER scope phi (tak ikut terlihat di back), jadi
    // snapshot kosong utk carried var -> fallback preLoopEnv = NILAI AWAL
    // (bug: `ulangi { n=n+1; jika ... lanjut }` -> n reset tiap lanjut,
    // berhenti_lanjut.rhc Test 5 infinite loop).
    NameValueMap contSnap;
    for (const auto& layer : env) {
        for (const auto& [n, v] : layer.vars) contSnap[n] = v;
    }
    loopStack.back().continues.emplace_back(currentBlock, contSnap);
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
    loopStack.push_back({condB, exitB, {}});  // lanjut -> condB

    // Phi loop-carried PRE-CREATED di header condB untuk SEMUA variabel
    // pra-loop: kondisi & body yang di-lowering setelah ini mereferensikan
    // phi (bukan nilai pra-loop), sehingga assignment di body benar-benar
    // loop-carried. Variabel yang tak berubah mendapat incoming self
    // (legal SSA, artinya nilai tetap).
    setCurrent(condB);
    std::vector<std::pair<std::string, ValueId>> carried;
    for (const auto& [name, oldValue] : preLoopEnv) {
        const TypeId phiType =
            builder.function(currentFunc).values[oldValue].type;
        // Nama phi HARUS unik per fungsi: dua loop `untuk i` berturut-turut
        // menyebabkan collision nama `%i` -> adopt remap salah, phi move
        // membaca home loop-1 -> loop kedua beku (rekursif.rhc).
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
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, oldValue);
        env.back().vars[name] = phi;
        carried.emplace_back(name, phi);
    }

    // Kondisi membaca phi.
    const ValueId condition = lowerExpr(stmt->condition);
    if (condition == InvalidValue) { loopStack.pop_back(); return false; }
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(bodyB), IrOperand::block(exitB)},
                          stmt->range.begin);

    // Body: assignment meng-update env; pemakaian nama membaca phi.
    setCurrent(bodyB);
    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { loopStack.pop_back(); capturePoppedEnv = false; return false; }
    capturePoppedEnv = false;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                              {IrOperand::block(condB)}, stmt->range.begin);
    }
    const auto continues = loopStack.back().continues;
    loopStack.pop_back();

    // Phi incoming back-edge: tiap pred condB selain pra-loop. Default =
    // nilai env akhir body (poppedEnv = scope body yang sudah pop);
    // situs lanjut memakai snapshot env-nya.
    // SNAPSHOT preds dulu: addPhiIncoming me-mutate fn.instrs, referensi
    // fungsi builder bisa stale bila dipegang selama iterasi.
    const std::vector<BlockId> condPreds(
        builder.function(currentFunc).blocks[condB].preds);
    for (const auto& [name, phi] : carried) {
        const auto itNew = poppedEnv.find(name);
        ValueId bodyEndValue =
            itNew == poppedEnv.end() ? InvalidValue : itNew->second;
        if (bodyEndValue == InvalidValue) {
            // Assignment update-through menulis ke layer scope phi (tak
            // ikut pop saat body) -> poppedEnv bisa melewatkannya (bug:
            // hitung=0 di bilangan_prima). Baca env terbuka; bila = phi
            // sendiri (tak berubah) pakai pra-loop (self-incoming memicu
            // dominasi-rusak saat loop-unroll).
            const ValueId cur = lookupRaw(name);
            bodyEndValue =
                (cur != InvalidValue && cur != phi) ? cur : preLoopEnv.at(name);
        }
        for (const BlockId pred : condPreds) {
            if (pred == preLoopBlock) continue;  // sudah ditambahkan
            ValueId incoming = bodyEndValue;
            for (const auto& cs : continues) {
                if (cs.first != pred) continue;
                const auto cv = cs.second.find(name);
                incoming = cv != cs.second.end() ? cv->second
                                                 : preLoopEnv.at(name);
                break;
            }
            builder.addPhiIncoming(currentFunc, phi, pred, incoming);
        }
    }

    // Nilai keluar loop = EXIT PHI di blok exit: i_out = phi[condB, phi].
    // Value didefinisikan di exit block sehingga mendominasi SEMUA
    // pemakaian setelah loop. (Restore langsung ke phi header merusak
    // dominasi saat loop-unroll peel: exit menjadi terjangkau tanpa lewat
    // header, dan peel hanya me-rewrite operand di blok exit itu sendiri.)
    for (const auto& [name, phi] : carried) {
        const TypeId outT = builder.function(currentFunc).values[phi].type;
        const ValueId out = builder.createPhi(outT, currentFunc, exitB, 0, name);
        builder.addPhiIncoming(currentFunc, out, condB, phi);
        env.back().vars[name] = out;
        builder.function(currentFunc).locals[name] = out;
    }

    setCurrent(exitB);
    return !builder.broken();
}

bool AstToIrTranslator::Impl::lowerDoWhileStatement(const DoWhileStmt* stmt) {
    if (failed || builder.broken() || !blockOpen()) return false;
    const NameValueMap preLoopEnv = env.back().vars;
    const BlockId preLoopBlock = currentBlock;

    const BlockId bodyB = builder.createBlock(currentFunc, "do.body");
    const BlockId condB = builder.createBlock(currentFunc, "do.cond");
    const BlockId exitB = builder.createBlock(currentFunc, "do.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(bodyB)}, stmt->range.begin);
    loopStack.push_back({bodyB, exitB, {}});  // lanjut -> body

    // Phi PRE-CREATED di header bodyB (preds: pra-loop, back-edge condB,
    // situs lanjut) — SEBELUM body, agar body & kondisi membaca nilai
    // loop-carried. bodyB mendominasi condB sehingga kondisi aman.
    setCurrent(bodyB);
    std::vector<std::pair<std::string, ValueId>> carried;
    for (const auto& [name, oldValue] : preLoopEnv) {
        const TypeId phiType =
            builder.function(currentFunc).values[oldValue].type;
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
        const ValueId phi = builder.createPhi(phiType, currentFunc, bodyB,
                                              0, phiName);
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, oldValue);
        env.back().vars[name] = phi;
        carried.emplace_back(name, phi);
    }

    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { loopStack.pop_back(); capturePoppedEnv = false; return false; }
    capturePoppedEnv = false;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                              {IrOperand::block(condB)}, stmt->range.begin);
    }

    // Kondisi HARUS membaca nilai AKHIR body utk carried var, bukan phi
    // header (stale, satu iterasi tertinggal). assignLocal update-through
    // menulis layer scope phi (tak terlihat di env.back()) -> lookup
    // kondisi membaca phi -> off-by-one (bug: dw.rhc `selama n < 5`
    // membaca n-1; output 1,2,4,5,7 vs ekspektasi 1,2,4,5). Nilai akhir
    // body = poppedEnv + pelengkap fnLocals (pola sama postBodyEnv;
    // loop var dari env.back() — fnLocals bisa berisi exit-phi loop
    // sebelumnya, dua loop `untuk n` berturut).
    {
        NameValueMap bodyEndEnv = poppedEnv;
        const NameValueMap& fnL = builder.function(currentFunc).locals;
        for (const auto& [name, phi] : carried) {
            if (bodyEndEnv.count(name)) continue;
            const auto itL = fnL.find(name);
            if (itL != fnL.end() && itL->second != phi) {
                bodyEndEnv[name] = itL->second;
            }
        }
        for (const auto& [n, v] : bodyEndEnv) {
            env.back().vars[n] = v;
        }
    }

    setCurrent(condB);
    const ValueId condition = lowerExpr(stmt->condition);
    if (condition == InvalidValue) { loopStack.pop_back(); return false; }
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(bodyB), IrOperand::block(exitB)},
                          stmt->range.begin);
    const auto continues = loopStack.back().continues;
    loopStack.pop_back();

    // Phi incoming back-edge: tiap pred bodyB selain pra-loop. Nilai yang
    // mengalir lewat condB = nilai env akhir body (kondisi tak mengubah
    // env); situs lanjut memakai snapshot env-nya.
    const std::vector<BlockId> bodyPreds(
        builder.function(currentFunc).blocks[bodyB].preds);
    for (const auto& [name, phi] : carried) {
        const auto itNew = poppedEnv.find(name);
        ValueId bodyEndValue =
            itNew == poppedEnv.end() ? InvalidValue : itNew->second;
        if (bodyEndValue == InvalidValue) {
            // Fallback: (1) env terbuka — assignment update-through menulis
            // ke layer scope phi (tak ikut pop) sehingga poppedEnv bisa
            // melewatkannya (bug: hitung=0 di bilangan_prima); (2) bila =
            // phi sendiri (tak berubah) pakai NILAI PRA-LOOP (loop-invariant,
            // mendominasi back-edge; self-incoming memicu dominasi-rusak saat
            // loop-unroll).
            const ValueId cur = lookupRaw(name);
            bodyEndValue =
                (cur != InvalidValue && cur != phi) ? cur : preLoopEnv.at(name);
        }
        for (const BlockId pred : bodyPreds) {
            if (pred == preLoopBlock) continue;  // sudah ditambahkan
            ValueId incoming = bodyEndValue;
            for (const auto& cs : continues) {
                if (cs.first != pred) continue;
                const auto cv = cs.second.find(name);
                incoming = cv != cs.second.end() ? cv->second
                                                 : preLoopEnv.at(name);
                break;
            }
            builder.addPhiIncoming(currentFunc, phi, pred, incoming);
        }
    }

    // Nilai keluar loop = EXIT PHI di blok exit: i_out = phi[condB, phi].
    // Value didefinisikan di exit block sehingga mendominasi SEMUA
    // pemakaian setelah loop. (Restore langsung ke phi header merusak
    // dominasi saat loop-unroll peel: exit menjadi terjangkau tanpa lewat
    // header, dan peel hanya me-rewrite operand di blok exit itu sendiri.)
    for (const auto& [name, phi] : carried) {
        const TypeId outT = builder.function(currentFunc).values[phi].type;
        const ValueId out = builder.createPhi(outT, currentFunc, exitB, 0, name);
        builder.addPhiIncoming(currentFunc, out, condB, phi);
        env.back().vars[name] = out;
        builder.function(currentFunc).locals[name] = out;
    }

    setCurrent(exitB);
    return !builder.broken();
}

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
    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { capturePoppedEnv = false; return false; }
    capturePoppedEnv = false;
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
            // incoming: dari akhir body (poppedEnv) & continue-sites.
            const auto& postBody = poppedEnv;
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
    for (const auto& [name, phi] : carriedFE) {
        const auto itAfter = poppedEnv.find(name);
        if (itAfter == poppedEnv.end()) continue;
        if (itAfter->second == phi) continue;  // tak berubah -> self
        builder.addPhiIncoming(currentFunc, phi, updateB, itAfter->second);
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

    // start/end (dan step opsional) dinilai sekali sebelum loop di blok
    // sekarang — keduanya loop-invariant (spec).
    const ValueId start = lowerExpr(stmt->startValue);
    if (start == InvalidValue) return false;
    const ValueId upper = lowerExpr(stmt->endValue);
    if (upper == InvalidValue) return false;
    const ValueId step = stmt->stepValue ? lowerExpr(stmt->stepValue)
                                         : InvalidValue;
    if (stmt->stepValue && step == InvalidValue) return false;

    NameValueMap preLoopEnv = env.back().vars;
    const BlockId preLoopBlock = currentBlock;

    const BlockId condB = builder.createBlock(currentFunc, "for.cond");
    const BlockId bodyB = builder.createBlock(currentFunc, "for.body");
    const BlockId updateB = builder.createBlock(currentFunc, "for.update");
    const BlockId exitB = builder.createBlock(currentFunc, "for.exit");

    builder.setTerminator(currentFunc, preLoopBlock, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);
    loopStack.push_back({updateB, exitB, {}});  // lanjut -> update

    // Phi loop-carried PRE-CREATED di header condB (pola sama dengan
    // while/do-while): kondisi & body membaca phi; variabel tak berubah
    // mendapat incoming self/merge. Loop variable (nama baru) ikut di-
    // phi-kan; kondisi & body membaca phi.
    setCurrent(condB);
    preLoopEnv[stmt->loopVariable] = start;   // agar merge latch melihatnya
    std::vector<std::pair<std::string, ValueId>> carried;
    for (const auto& [name, oldValue] : preLoopEnv) {
        const TypeId phiType =
            builder.function(currentFunc).values[oldValue].type;
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
        builder.addPhiIncoming(currentFunc, phi, preLoopBlock, oldValue);
        env.back().vars[name] = phi;
        carried.emplace_back(name, phi);
    }

    // Kondisi: loopVariable <= upper — 'sampai' INKLUSIF (dari 1 sampai 5
    // berarti 1,2,3,4,5; sejalan dengan padanan selama i <= 5 di korpus).
    // Langkah negatif konstan -> CmpGe (pola pristine).
    const ValueId loopVar = lookupLocal(stmt->loopVariable);
    IrOpcode cmpOp = IrOpcode::CmpLe;
    if (step != InvalidValue) {
        // deteksi langkah negatif konstan (ConstInt 1-ImmV negatif)
        const IrInstr* stepDef = nullptr;
        if (step < builder.function(currentFunc).values.size()) {
            const IrValue& sv = builder.function(currentFunc).values[step];
            if (sv.kind == IrValue::Kind::Instr &&
                sv.payload < builder.function(currentFunc).instrs.size()) {
                stepDef = &builder.function(currentFunc).instrs[sv.payload];
            }
        }
        if (stepDef && stepDef->op == IrOpcode::ConstInt &&
            stepDef->operands.size() == 1 &&
            stepDef->operands[0].kind == IrOperandKind::ImmV) {
            const std::int32_t stepImm =
                static_cast<std::int32_t>(stepDef->operands[0].id);
            if (stepImm < 0) cmpOp = IrOpcode::CmpGe;
        }
    }
    const TypeId condType =
        builder.function(currentFunc).values[loopVar].type;
    const ValueId condition = builder.emit(
        cmpOp, condType, {IrOperand::value(loopVar), IrOperand::value(upper)},
        stmt->range.begin);
    if (condition == InvalidValue) { loopStack.pop_back(); return false; }
    builder.setTerminator(currentFunc, condB, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(bodyB), IrOperand::block(exitB)},
                          stmt->range.begin);

    // Body.
    setCurrent(bodyB);
    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { loopStack.pop_back(); capturePoppedEnv = false; return false; }
    capturePoppedEnv = false;
    // poppedEnv (overwrite) = scope TERAKHIR yang pop saja; assignment
    // update-through menulis ke layer scope phi (tak ikut pop saat body)
    // sehingga bisa terlewat (bug: hitung=0 di bilangan_prima — carried
    // var di-assign di blok bersarang dalam untuk-range). Lengkapi dari
    // env terbuka utk nama carried yang berubah (cur != phi).
    NameValueMap postBodyEnv = poppedEnv;
    // Sumber nilai akhir body: function locals — assignLocal SELALU
    // menulis function(currentFunc).locals (ter-update berurutan,
    // regardless scope pop). poppedEnv (overwrite = scope pop terakhir
    // saja) melewatkan assignment di blok bersarang (bug: hitung=0 di
    // bilangan_prima). Nama tak pernah di-assign: locals = pra-loop
    // (≠ phi) -> sama dgn fallback, tak mengubah hasil merge.
    const NameValueMap& fnLocals = builder.function(currentFunc).locals;
    for (const auto& [name, phi] : carried) {
        if (postBodyEnv.count(name)) continue;
        if (name == stmt->loopVariable) {
            // Loop var: binding env.back() = phi header loop INI (ditulis
            // saat header dibangun). fnLocals bisa berisi sisa exit-phi
            // loop SEBELUMNYA bila dua loop `untuk i` berturut (bug
            // lp5/rekursif: latch incoming = %i loop-1 -> hang).
            const auto itE = env.back().vars.find(name);
            if (itE != env.back().vars.end() && itE->second != phi) {
                postBodyEnv[name] = itE->second;  // assignment di body loop
            }
            continue;  // bila == phi: tak berubah -> incoming langsung phi
        }
        const auto itL = fnLocals.find(name);
        if (itL != fnLocals.end() && itL->second != phi) {
            postBodyEnv[name] = itL->second;
        }
    }
    const BlockId bodyEnd = currentBlock;
    const auto continues = loopStack.back().continues;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, bodyEnd, IrOpcode::Br,
                              {IrOperand::block(updateB)}, stmt->range.begin);
    }
    // Merge latch di updateB atas nilai yang datang dari akhir body dan
    // situs lanjut (bila perbedaan -> phi di updateB lewat mergeAtJoin).
    setCurrent(updateB);
    std::vector<std::pair<BlockId, const NameValueMap*>> latchPreds;
    if (bodyEnd != InvalidBlock && blockOpen()) {
        // bodyEnd baru saja di-terminasi; cek lewat CFG.
    }
    // (preds updateB: bodyEnd bila body berakhir terbuka + tiap lanjut)
    {
        const IrFunction& f = builder.function(currentFunc);
        for (const BlockId p : f.blocks[updateB].preds) {
            if (p == bodyEnd) {
                latchPreds.emplace_back(p, &postBodyEnv);
            } else {
                for (const auto& cs : continues) {
                    if (cs.first == p) {
                        latchPreds.emplace_back(p, &cs.second);
                        break;
                    }
                }
            }
        }
    }
    NameValueMap latchEnv = preLoopEnv;
    if (!latchPreds.empty()) {
        latchEnv = mergeAtJoin(updateB, latchPreds, preLoopEnv);
    }

    // Phi merge latch khusus loop var di updateB: merepresentasikan nilai
    // loop var yang masuk ke updateB (dari akhir body / situs lanjut).
    // Tanpa ini, loop var self-referensi (inc(phi condB) -> incoming
    // condB = hasil inc itu sendiri).
    ValueId latchPhi = InvalidValue;
    if (!latchPreds.empty()) {
        // Nama latch phi HARUS unik per fungsi: dua loop `untuk i` berturut
        // -> collision `i_latch` -> phi move loop-2 baca home loop-1 (hang).
        std::string latchName = stmt->loopVariable + "_latch";
        {
            const auto& vv = builder.function(currentFunc).values;
            auto taken = [&](const std::string& nm) {
                for (const auto& v : vv) {
                    if (v.name == nm) return true;
                }
                return builder.function(currentFunc).locals.count(nm) > 0;
            };
            if (taken(latchName)) {
                int n = 2;
                while (taken(latchName + "_" + std::to_string(n))) ++n;
                latchName += "_" + std::to_string(n);
            }
        }
        latchPhi = builder.createPhi(condType, currentFunc, updateB, 0,
                                     latchName);
        for (const auto& [pred, envMap] : latchPreds) {
            const auto itV = envMap->find(stmt->loopVariable);
            builder.addPhiIncoming(
                currentFunc, latchPhi, pred,
                itV == envMap->end()
                    ? env.back().vars[stmt->loopVariable]
                    : itV->second);
        }
    }
    const ValueId latchLoopVar =
        latchPhi != InvalidValue ? latchPhi : lookupLocal(stmt->loopVariable);
    ValueId nextValue = InvalidValue;
    if (step != InvalidValue) {
        nextValue = builder.emit(IrOpcode::Add, condType,
                                 {IrOperand::value(latchLoopVar),
                                  IrOperand::value(step)},
                                 stmt->range.begin, stmt->loopVariable + "_next");
        if (builder.broken()) return false;
    } else {
        nextValue = builder.emit(IrOpcode::Inc, condType,
                                 {IrOperand::value(latchLoopVar)},
                                 stmt->range.begin, stmt->loopVariable + "_next");
        if (builder.broken()) return false;
    }
    // Merge-phi di bodyEnd utk carried var yang berubah. bodyEnd bisa
    // menerima jalur yang MEMOTONG (mis. else dari `jika` di body yang
    // lompat ke merge latch TANPA lewat blok def flow — kasus
    // bilangan_prima: non-prima tak melewati join def %v17). Maka:
    //   bodyEnd: %m = phi [ defBlock(flow), flow ], [ pred-lain, phi ]
    // defBlock(flow) = blok instr def flow (mendominasi dirinya -> edge
    // trivial); pred lain (jalur potong) membawa phi header (nilai TAK
    // BERUBAH — benar karena assignment terlewati). %m def di bodyEnd ->
    // mendominasi edge bodyEnd->updateB trivially. Def langsung di
    // updateB, echo-phi, & clone SEMUANYA gagal utk kasus potongan jalur.
    std::unordered_map<std::string, ValueId> echo;
    for (const auto& [name, phi] : carried) {
        if (name == stmt->loopVariable) continue;
        const auto itL = latchEnv.find(name);
        const ValueId v = itL == latchEnv.end() ? phi : itL->second;
        if (v == phi) continue;  // tak berubah -> langsung phi
        const auto& fnv = builder.function(currentFunc);
        // Blok def flow.
        BlockId defBv = InvalidBlock;
        if (fnv.values[v].kind == IrValue::Kind::Instr &&
            fnv.values[v].payload < fnv.instrs.size()) {
            for (std::uint32_t bi = 0; bi < fnv.blocks.size(); ++bi) {
                for (const auto ii : fnv.blocks[bi].insts) {
                    if (ii == fnv.values[v].payload) { defBv = bi; break; }
                }
                if (defBv != InvalidBlock) break;
            }
        }
        if (defBv == InvalidBlock || defBv == bodyEnd) {
            echo[name] = v;  // def di bodyEnd/tak diketahui: langsung pakai
            continue;
        }
        const TypeId ty = fnv.values[phi].type;
        const ValueId e = builder.createPhi(ty, currentFunc, bodyEnd);
        for (const BlockId P : fnv.blocks[bodyEnd].preds) {
            builder.addPhiIncoming(currentFunc, e, P,
                                   (P == defBv) ? v : phi);
        }
        echo[name] = e;
    }

    env.back().vars = latchEnv;
    env.back().vars[stmt->loopVariable] = nextValue;
    builder.setTerminator(currentFunc, updateB, IrOpcode::Br,
                          {IrOperand::block(condB)}, stmt->range.begin);

    // Phi incoming (updateB): loop var -> nextValue; lainnya -> echo phi
    // (bila berubah) atau latchEnv/phi (tak berubah).
    for (const auto& [name, phi] : carried) {
        ValueId incoming = nextValue;
        if (name != stmt->loopVariable) {
            const auto itE = echo.find(name);
            if (itE != echo.end()) {
                incoming = itE->second;
            } else {
                const auto itL = latchEnv.find(name);
                incoming = itL == latchEnv.end() ? phi : itL->second;
            }
        }
        builder.addPhiIncoming(currentFunc, phi, updateB, incoming);
    }

    // Nilai keluar loop = EXIT PHI di blok exit: i_out = phi[condB, phi].
    // Value didefinisikan di exit block sehingga mendominasi SEMUA
    // pemakaian setelah loop. (Restore langsung ke phi header merusak
    // dominasi saat loop-unroll peel: exit menjadi terjangkau tanpa lewat
    // header, dan peel hanya me-rewrite operand di blok exit itu sendiri.)
    for (const auto& [name, phi] : carried) {
        const TypeId outT = builder.function(currentFunc).values[phi].type;
        const ValueId out = builder.createPhi(outT, currentFunc, exitB, 0, name);
        builder.addPhiIncoming(currentFunc, out, condB, phi);
        env.back().vars[name] = out;
        builder.function(currentFunc).locals[name] = out;
    }

    setCurrent(exitB);
    return !builder.broken();
}

} // namespace rhc
