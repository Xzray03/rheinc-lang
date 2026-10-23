#include "RhAST/AstToIrTranslatorImpl.h"
#include "RhIR/IrDomAnalysis.h"

#include "RhParser/Stmt.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

namespace rhc {

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
    const bool savedCapture = capturePoppedEnv;
    capturePoppedEnv = true; poppedEnv.clear();
    if (!lowerStmt(stmt->body)) { loopStack.pop_back(); capturePoppedEnv = savedCapture; return false; }
    capturePoppedEnv = savedCapture;
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
