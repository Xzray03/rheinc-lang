#include "RhAST/AstToIrTranslatorImpl.h"

#include <utility>

namespace rhc {

// Penyisipan phi di join point (spesifikasi Bagian 4.2).
// Untuk tiap nama yang hidup sebelum percabangan (fallback): bila nilai
// berbeda antar pred, sisipkan phi dengan satu incoming per pred yang
// benar-benar mengalir ke join; bila sama, pakai nilai bersama tanpa phi.
// Nama yang lahir di dalam satu cabang saja tidak digabung — sesuai
// aturan scope (tidak hidup setelah join).
AstToIrTranslator::Impl::NameValueMap
AstToIrTranslator::Impl::mergeAtJoin(
        BlockId join,
        const std::vector<std::pair<BlockId, const NameValueMap*>>& preds,
        const NameValueMap& fallback) {
    NameValueMap merged = fallback;
    if (failed || builder.broken()) return merged;

    for (const auto& [name, preValue] : fallback) {
        ValueId common = InvalidValue;
        bool allSame = true;
        bool anyPredHas = false;
        for (const auto& [predBlock, predEnv] : preds) {
            ValueId v = preValue;
            if (predEnv) {
                const auto it = predEnv->find(name);
                if (it != predEnv->end()) {
                    v = it->second;
                    anyPredHas = true;
                }
            }
            if (common == InvalidValue) {
                common = v;
            } else if (v != common) {
                allSame = false;
            }
        }
        if (!anyPredHas || allSame) {
            merged[name] = allSame ? common : preValue;
            continue;
        }

        // Nilai berbeda antar pred -> phi.
        const TypeId phiType =
            builder.function(currentFunc).values[preValue].type;
        const ValueId phi = builder.createPhi(
            phiType, currentFunc, join, 0, name);
        for (const auto& [predBlock, predEnv] : preds) {
            ValueId v = preValue;
            if (predEnv) {
                const auto it = predEnv->find(name);
                if (it != predEnv->end()) v = it->second;
            }
            builder.addPhiIncoming(currentFunc, phi, predBlock, v);
        }
        merged[name] = phi;
    }
    return merged;
}

} // namespace rhc
