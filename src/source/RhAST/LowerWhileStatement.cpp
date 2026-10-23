#include "RhAST/AstToIrTranslatorImpl.h"
#include "RhIR/IrDomAnalysis.h"

#include "RhParser/Stmt.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

namespace rhc {

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
    const bool savedCapture = capturePoppedEnv;
    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { loopStack.pop_back(); capturePoppedEnv = savedCapture; return false; }
    capturePoppedEnv = savedCapture;
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

    const bool savedCapture = capturePoppedEnv;
    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { loopStack.pop_back(); capturePoppedEnv = savedCapture; return false; }
    capturePoppedEnv = savedCapture;
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
    // Nilai yang mengalir ke condB (kondisi tak mengubah env): poppedEnv
    // (scope body terpop) + pelengkap fnLocals utk carried (assignment di
    // scope yang sudah pop tidak terlihat di env terbuka — bug hitung=0
    // bilangan_prima). Dipakai JUGA utk exit phi di bawah.
    NameValueMap bodyEndEnv = poppedEnv;
    {
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
        // Incoming dari condB HARUS nilai AKHIR body (yang mengalir lewat
        // condB), bukan phi header: header phi (di bodyB) dievaluasi saat
        // START body — nilai yang keluar lewat condB = bodyEndEnv (bug:
        // `ulangi { m = m+1 } selama l` membaca m satu iterasi tertinggal).
        const auto itE = bodyEndEnv.find(name);
        builder.addPhiIncoming(currentFunc, out, condB,
                               itE != bodyEndEnv.end() ? itE->second : phi);
        env.back().vars[name] = out;
        builder.function(currentFunc).locals[name] = out;
    }

    setCurrent(exitB);
    return !builder.broken();
}

} // namespace rhc
