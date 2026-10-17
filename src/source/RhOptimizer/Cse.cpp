#include "RhOptimizer/Cse.h"
#include "RhOptimizer/DomAnalysis.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <algorithm>
#include <functional>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace rhc {

namespace {

// Kunci ekspresi: opcode + resultType + operand (kind,id per slot).
struct ExprKey {
    std::uint8_t op = 0;
    std::uint32_t resultType = 0;
    std::vector<std::pair<std::uint8_t, std::uint32_t>> operands;

    bool operator==(const ExprKey& o) const {
        return op == o.op && resultType == o.resultType &&
               operands == o.operands;
    }
};

struct ExprKeyHash {
    std::size_t operator()(const ExprKey& k) const {
        std::size_t h = k.op * 131u + k.resultType;
        for (const auto& p : k.operands) {
            h = h * 257u + p.first * 17u + p.second;
        }
        return h;
    }
};

bool isCommutative(IrOpcode op) {
    return op == IrOpcode::Add || op == IrOpcode::Mul ||
           op == IrOpcode::BitAnd || op == IrOpcode::BitOr ||
           op == IrOpcode::BitXor || op == IrOpcode::CmpEq ||
           op == IrOpcode::CmpNe;
}

ExprKey keyOf(const IrInstr& instr) {
    ExprKey k;
    k.op = static_cast<std::uint8_t>(instr.op);
    k.resultType = instr.resultType;
    k.operands.reserve(instr.operands.size());
    for (const IrOperand& op : instr.operands) {
        k.operands.emplace_back(static_cast<std::uint8_t>(op.kind), op.id);
    }
    if (isCommutative(instr.op) && k.operands.size() == 2 &&
        k.operands[1] < k.operands[0]) {
        std::swap(k.operands[0], k.operands[1]);
    }
    return k;
}

} // namespace

IrPassResult CommonSubexpressionElimination::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.blocks.empty()) continue;
        const opt::DomInfo dom = opt::computeDom(fn);

        std::unordered_map<ExprKey, ValueId, ExprKeyHash> available;
        std::vector<std::pair<ExprKey, bool>> added;  // (kunci, benar-benar baru)

        // Alihkan semua USE dari value oldV -> newV di seluruh fungsi.
        auto rewriteUses = [&](ValueId oldV, ValueId newV,
                               std::uint32_t& count) -> bool {
            for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                IrBlock& blk = fn.blocks[b];
                for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                    const InstrId iid = blk.insts[pos];
                    if (iid >= fn.instrs.size()) continue;
                    // snapshot operands (editor mutasi vector)
                    const std::vector<IrOperand> ops =
                        fn.instrs[iid].operands;
                    for (std::size_t oi = 0; oi < ops.size(); ++oi) {
                        if (ops[oi].kind == IrOperandKind::ValueV &&
                            ops[oi].id == oldV) {
                            if (!editor.replaceOperandAt(f, b, pos, oi,
                                    IrOperand::value(newV)).ok) {
                                return false;
                            }
                            ++count;
                        }
                    }
                }
            }
            return true;
        };

        // Traversal preorder pohon dominator dengan map bercakup:
        // masuk block -> proses instruksi, tambah kunci baru; keluar
        // block -> tarik kembali semua kunci yang ditambahkan di sana.
        // Availability yang tersisa pasti berasal dari block dominator.
        std::uint32_t rewrites = 0;
        const std::function<bool(BlockId)> walk =
            [&](BlockId b) -> bool {
                const std::size_t addedBase = added.size();
                const IrBlock& blk = fn.blocks[b];
                for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                    const InstrId iid = blk.insts[pos];
                    if (iid >= fn.instrs.size()) continue;
                    const IrInstr instr = fn.instrs[iid];  // snapshot
                    if (instr.resultType == InvalidType ||
                        !irOpIsPureForDce(instr.op)) {
                        continue;
                    }
                    ValueId vSelf = InvalidValue;
                    for (std::uint32_t v = 0; v < fn.values.size(); ++v) {
                        if (fn.values[v].kind == IrValue::Kind::Instr &&
                            fn.values[v].payload == iid) {
                            vSelf = v;
                            break;
                        }
                    }
                    if (vSelf == InvalidValue) continue;

                    const ExprKey k = keyOf(instr);
                    const auto it = available.find(k);
                    if (it != available.end()) {
                        std::uint32_t n = 0;
                        if (!rewriteUses(vSelf, it->second, n)) {
                            return false;
                        }
                        rewrites += n;
                        // Instruksi redundan tidak ditambahkan ke map;
                        // value-nya menjadi mati dan disapu DCE nanti.
                    } else {
                        available.emplace(k, vSelf);
                        added.emplace_back(k, true);
                    }
                }
                for (const BlockId child : dom.children[b]) {
                    if (!walk(child)) return false;
                }
                // Rollback cakup block ini.
                while (added.size() > addedBase) {
                    available.erase(added.back().first);
                    added.pop_back();
                }
                return true;
            };
        if (!walk(0)) { result.ok = false; return result; }

        // Sapu instruksi mati hasil eliminasi.
        std::uint32_t removedDead = 0;
        const IrEditorReport dceStep =
            editor.removeDeadInstrs(f, removedDead);
        if (!dceStep.ok) { result.ok = false; return result; }
        if (rewrites > 0 || removedDead > 0) {
            result.changed = true;
            result.folded += rewrites + removedDead;
        }
    }
    return result;
}

} // namespace rhc
