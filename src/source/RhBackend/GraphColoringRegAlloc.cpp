// GraphColoringRegAlloc.cpp - Chaitin-style graph coloring, k = 5.
//
// Tahap:
//   1. Liveness dataflow (use/def per blok; phi-incoming sebagai use pada
//      blok pred; fixpoint mundur).
//   2. Graf interferensi: tepi (d, v) bila def d hidup bersamaan v;
//      pasangan phi-result dalam blok sama saling interfere (parallel
//      copy). Interval program-order (konservatif, aman untuk loop).
//   3. Simplify: node deg < k ditumpuk; sisanya optimistic spill.
//   4. Select: warna bebas terendah; tak ada warna -> spill (slot).
//
// Eligible: skalar <=8B kind Int/Bool/Pointer/FnPtr (bukan Float).

#include "RhBackend/RegAlloc.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace rhc {

namespace {

const char* kColors[5] = {"rbx", "r12", "r13", "r14", "r15"};
constexpr std::uint32_t kK = 5;

class GraphColoring final : public RegAlloc {
public:
    RegAllocResult allocate(const IrModule& module, const IrFunction& fn,
                            std::vector<RhBackendError>& errors) override {
        module_ = &module;
        fn_ = &fn;
        errors_ = &errors;
        // Reset penuh per fungsi — kalau tidak, homes (dan state lain)
        // dari fungsi sebelumnya bocor ke fungsi berikutnya (bug: value
        // F2 ambil register milik value F1 yang id-nya sama → loop
        // `untuk i` kedua membaca home loop pertama → infinite loop).
        // Reset PER FUNGSI: homes dari fungsi sebelumnya bocor ke fungsi
        // berikutnya bila result_ tidak di-reassign (bug: value F2 ambil
        // register milik value F1 dengan id sama -> phi move loop-2
        // membaca home loop pertama -> loop `untuk i` kedua beku).
        result_ = RegAllocResult{};

        collectEligible();
        if (eligible_.empty()) {
            result_.ok = true;
            return result_;
        }
        computeLiveness();
        buildInterference();
        simplifyAndSelect();
        result_.ok = true;
        return result_;
    }

private:
    bool isEligible(ValueId v) const {
        if (v >= fn_->values.size()) return false;
        const IrValue& val = fn_->values[v];
        if (val.type == InvalidType || val.type >= module_->types.size()) {
            return false;
        }
        const IrType& t = module_->types[val.type];
        if (t.sizeBytes > 8) return false;
        return t.kind == IrTypeKind::Int || t.kind == IrTypeKind::Bool ||
               t.kind == IrTypeKind::Pointer || t.kind == IrTypeKind::FnPtr;
    }

    void collectEligible() {
        for (ValueId v = 0; v < fn_->values.size(); ++v) {
            if (isEligible(v)) eligible_.insert(v);
        }
    }

    // ---- liveness ----
    void computeLiveness() {
        const std::uint32_t nb = fn_->blocks.size();
        use_.assign(nb, {});
        def_.assign(nb, {});
        liveIn_.assign(nb, {});
        liveOut_.assign(nb, {});
        phiIn_.assign(nb, {});

        auto addUse = [&](std::uint32_t b, ValueId v) {
            if (eligible_.count(v) && !def_[b].count(v)) use_[b].insert(v);
        };
        auto addDef = [&](std::uint32_t b, ValueId v) {
            if (eligible_.count(v)) {
                def_[b].insert(v);
                use_[b].erase(v);
            }
        };

        for (std::uint32_t b = 0; b < nb; ++b) {
            const IrBlock& blk = fn_->blocks[b];
            for (const InstrId iid : blk.insts) {
                if (iid >= fn_->instrs.size()) continue;
                const IrInstr& inst = fn_->instrs[iid];
                // phi: hasil = def; incoming ValueV = use di blok pred.
                if (inst.op == IrOpcode::Phi) {
                    ValueId r = InvalidValue;
                    for (ValueId v = 0; v < fn_->values.size(); ++v) {
                        const IrValue& val = fn_->values[v];
                        if (val.kind == IrValue::Kind::Instr &&
                            val.payload == iid) {
                            r = v;
                            break;
                        }
                    }
                    if (r != InvalidValue) addDef(b, r);
                    for (std::size_t k = 0; k + 1 < inst.operands.size();
                         k += 2) {
                        if (inst.operands[k].kind == IrOperandKind::BlockV &&
                            inst.operands[k].id < nb &&
                            inst.operands[k + 1].kind ==
                                IrOperandKind::ValueV) {
                            // Incoming dipakai DI TEPI oleh phi-move emitter:
                            // wajib live-OUT di blok pred (bila tidak,
                            // dua incoming dikawinkan satu register dan
                            // move phi membaca nilai salah -> loop beku).
                            phiIn_[inst.operands[k].id].insert(
                                inst.operands[k + 1].id);
                        }
                    }
                    continue;
                }
                for (const IrOperand& op : inst.operands) {
                    if (op.kind == IrOperandKind::ValueV) addUse(b, op.id);
                }
                ValueId r = InvalidValue;
                for (ValueId v = 0; v < fn_->values.size(); ++v) {
                    const IrValue& val = fn_->values[v];
                    if (val.kind == IrValue::Kind::Instr &&
                        val.payload == iid) {
                        r = v;
                        break;
                    }
                }
                if (r != InvalidValue) addDef(b, r);
            }
        }

        // Fixpoint mundur.
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::uint32_t b = nb; b-- > 0;) {
                std::set<ValueId> out;
                for (const BlockId s : fn_->blocks[b].succs) {
                    if (s < nb) {
                        out.insert(liveIn_[s].begin(), liveIn_[s].end());
                    }
                }
                // Nilai phi-incoming harus hidup sampai akhir blok pred.
                out.insert(phiIn_[b].begin(), phiIn_[b].end());
                std::set<ValueId> in = use_[b];
                for (ValueId v : out) {
                    if (!def_[b].count(v)) in.insert(v);
                }
                if (in != liveIn_[b] || out != liveOut_[b]) {
                    liveIn_[b] = std::move(in);
                    liveOut_[b] = std::move(out);
                    changed = true;
                }
            }
        }
    }

    // ---- interferensi ----
    void addEdge(ValueId a, ValueId b) {
        if (a == b || !eligible_.count(a) || !eligible_.count(b)) return;
        adj_[a].insert(b);
        adj_[b].insert(a);
    }

    void buildInterference() {
        const std::uint32_t nb = fn_->blocks.size();
        for (ValueId v : eligible_) adj_[v] = {};
        for (std::uint32_t b = 0; b < nb; ++b) {
            const IrBlock& blk = fn_->blocks[b];
            // Pasangan phi-result dalam blok sama saling interfere.
            std::vector<ValueId> phis;
            for (const InstrId iid : blk.insts) {
                if (iid >= fn_->instrs.size() ||
                    fn_->instrs[iid].op != IrOpcode::Phi) {
                    continue;
                }
                for (ValueId v : eligible_) {
                    const IrValue& val = fn_->values[v];
                    if (val.kind == IrValue::Kind::Instr &&
                        val.payload == iid) {
                        phis.push_back(v);
                        break;
                    }
                }
            }
            for (std::size_t i = 0; i < phis.size(); ++i) {
                for (std::size_t j = i + 1; j < phis.size(); ++j) {
                    addEdge(phis[i], phis[j]);
                }
            }
            // Scan mundur: live = liveOut; def vs live -> tepi.
            std::set<ValueId> live = liveOut_[b];
            for (auto it = blk.insts.rbegin(); it != blk.insts.rend(); ++it) {
                const InstrId iid = *it;
                if (iid >= fn_->instrs.size()) continue;
                const IrInstr& inst = fn_->instrs[iid];
                // def di titik ini (hasil instruksi non-phi; phi def di awal).
                ValueId d = InvalidValue;
                for (ValueId v : eligible_) {
                    const IrValue& val = fn_->values[v];
                    if (val.kind == IrValue::Kind::Instr &&
                        val.payload == iid) {
                        d = v;
                        break;
                    }
                }
                if (d != InvalidValue && inst.op != IrOpcode::Phi) {
                    for (ValueId v : live) addEdge(d, v);
                    live.erase(d);
                }
                if (inst.op != IrOpcode::Phi) {
                    for (const IrOperand& op : inst.operands) {
                        if (op.kind == IrOperandKind::ValueV &&
                            eligible_.count(op.id)) {
                            live.insert(op.id);
                        }
                    }
                }
            }
            // liveIn pairs juga interfere (masuk blok bersamaan hidup).
            for (auto i = live.begin(); i != live.end(); ++i) {
                for (auto j = std::next(i); j != live.end(); ++j) {
                    addEdge(*i, *j);
                }
            }
            // Pasangan yang hidup bersama di AKHIR blok (liveOut + phi
            // incoming) ikut interfere: move phi mengeksekusinya paralel.
            std::set<ValueId> tail = liveOut_[b];
            tail.insert(phiIn_[b].begin(), phiIn_[b].end());
            for (auto i = tail.begin(); i != tail.end(); ++i) {
                for (auto j = std::next(i); j != tail.end(); ++j) {
                    addEdge(*i, *j);
                }
            }
        }
    }

    // ---- simplify + select ----
    void simplifyAndSelect() {
        std::unordered_map<ValueId, std::uint32_t> degree;
        for (ValueId v : eligible_) degree[v] = adj_[v].size();

        std::vector<ValueId> stack;
        std::unordered_set<ValueId> removed;
        std::set<ValueId> remaining(eligible_.begin(), eligible_.end());

        while (!remaining.empty()) {
            ValueId pick = InvalidValue;
            for (ValueId v : remaining) {
                if (degree[v] < kK) { pick = v; break; }
            }
            if (pick == InvalidValue) {
                // Optimistic spill: node ber-degree tertinggi.
                for (ValueId v : remaining) {
                    if (pick == InvalidValue || degree[v] > degree[pick]) {
                        pick = v;
                    }
                }
            }
            stack.push_back(pick);
            removed.insert(pick);
            remaining.erase(pick);
            for (ValueId u : adj_[pick]) {
                if (removed.count(u)) continue;
                if (degree[u] > 0) --degree[u];
            }
        }

        // Select: warna bebas terendah; tak ada -> spill (slot).
        std::unordered_map<ValueId, std::uint32_t> colorOf;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            const ValueId v = *it;
            bool used[5] = {false, false, false, false, false};
            for (ValueId u : adj_[v]) {
                const auto c = colorOf.find(u);
                if (c != colorOf.end()) used[c->second] = true;
            }
            std::uint32_t chosen = 5;
            for (std::uint32_t c = 0; c < kK; ++c) {
                if (!used[c]) { chosen = c; break; }
            }
            if (chosen < kK) {
                colorOf[v] = chosen;
                RegLoc loc;
                loc.inReg = true;
                loc.reg = kColors[chosen];
                result_.homes.emplace(v, loc);
            }
            // else: spill — tak ada home; emitter pakai slot naive.
        }

        for (std::uint32_t c = 0; c < kK; ++c) {
            bool any = false;
            for (const auto& kv : colorOf) {
                if (kv.second == c) { any = true; break; }
            }
            if (any) result_.usedCalleeSaved.emplace_back(kColors[c]);
        }
    }

    const IrModule* module_ = nullptr;
    const IrFunction* fn_ = nullptr;
    std::vector<RhBackendError>* errors_ = nullptr;
    RegAllocResult result_;

    std::unordered_set<ValueId> eligible_;
    std::vector<std::set<ValueId>> use_, def_, liveIn_, liveOut_;
    std::vector<std::set<ValueId>> phiIn_;  // incoming phi per blok pred
    std::unordered_map<ValueId, std::set<ValueId>> adj_;
};

}  // namespace

RegAlloc* createGraphColoringRegAlloc() { return new GraphColoring(); }

}  // namespace rhc
