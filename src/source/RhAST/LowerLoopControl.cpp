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

} // namespace rhc
