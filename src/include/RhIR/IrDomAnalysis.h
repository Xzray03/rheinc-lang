#pragma once

// Analisis dominator untuk RhIR (verifier, dan pass optimizer via
// header kompatibilitas RhOptimizer/DomAnalysis.h).

#include "RhIR/IrFunction.h"
#include "RhIR/IrIds.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <vector>

namespace rhc {

struct DomInfo {
    std::uint32_t n = 0;
    std::uint32_t words = 0;
    std::vector<std::vector<std::uint64_t>> dom;  // dom[b] = himpunan
    std::vector<BlockId> idom;                    // immediate dominator
    std::vector<std::vector<BlockId>> children;   // pohon dominator

    // dominates(x, y) == true bila y mendominasi x.
    bool dominates(std::uint32_t b, std::uint32_t i) const {
        return b < n && i < n && ((dom[b][i >> 6] >> (i & 63)) & 1ull) != 0;
    }
};

// Dominator analysis (bitset per block): dataflow iteratif
// dom[b] = {b} U (AND dom[p]); lalu idom + pohon dominator.
inline DomInfo computeDom(const IrFunction& fn) {
    DomInfo d;
    d.n = static_cast<std::uint32_t>(fn.blocks.size());
    d.words = (d.n + 63) / 64;
    d.dom.assign(d.n, std::vector<std::uint64_t>(d.words, 0));
    for (std::uint32_t b = 0; b < d.n; ++b) {
        for (std::uint32_t i = 0; i < d.n; ++i) {
            const std::uint64_t m = 1ull << (i & 63);
            if (b == 0) {
                if (i == 0) d.dom[b][i >> 6] |= m;
            } else {
                d.dom[b][i >> 6] |= m;
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::uint32_t b = 1; b < d.n; ++b) {
            const IrBlock& blk = fn.blocks[b];
            if (blk.preds.empty()) continue;  // tak terjangkau
            for (std::uint32_t w = 0; w < d.words; ++w) {
                std::uint64_t acc = ~0ull;
                for (const BlockId p : blk.preds) {
                    if (p < d.n) acc &= d.dom[p][w];
                }
                acc |= 1ull << (b & 63);
                if (acc != d.dom[b][w]) {
                    d.dom[b][w] = acc;
                    changed = true;
                }
            }
        }
    }
    d.idom.assign(d.n, InvalidBlock);
    d.children.assign(d.n, {});
    for (std::uint32_t b = 1; b < d.n; ++b) {
        std::vector<std::uint32_t> strict;
        for (std::uint32_t i = 0; i < d.n; ++i) {
            if (i != b && d.dominates(b, i)) strict.push_back(i);
        }
        for (const std::uint32_t c : strict) {
            bool immediate = true;
            for (const std::uint32_t o : strict) {
                if (o != c && d.dominates(c, o)) {
                    immediate = false;
                    break;
                }
            }
            if (immediate) { d.idom[b] = c; break; }
        }
        if (d.idom[b] != InvalidBlock) {
            d.children[d.idom[b]].push_back(b);
        }
    }
    return d;
}

} // namespace rhc
