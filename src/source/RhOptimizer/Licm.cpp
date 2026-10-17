#include "RhOptimizer/Licm.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"
#include "RhOptimizer/DomAnalysis.h"

#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace rhc {

namespace {

// Op murni yang tetap aman di-speculate walau jalur aslinya mungkin tak
// pernah mengeksekusinya: hanya Div/Mod/DivFloor/Pow yang bisa trap
// (pembagian nol) sehingga butuh syarat dominasi-exit tambahan.
bool mayTrapOnExtremes(IrOpcode op) {
    return op == IrOpcode::Div || op == IrOpcode::Mod ||
           op == IrOpcode::DivFloor || op == IrOpcode::Pow;
}

struct Loop {
    BlockId header = InvalidBlock;
    std::vector<std::uint8_t> inLoop;  // per block id
    std::uint32_t size = 0;
};

// Natural loop untuk back-edge latch -> header.
Loop naturalLoop(const IrFunction& fn, BlockId header, BlockId latch) {
    Loop L;
    L.header = header;
    L.inLoop.assign(fn.blocks.size(), 0);
    std::vector<BlockId> stack{latch};
    L.inLoop[latch] = 1;
    while (!stack.empty()) {
        const BlockId b = stack.back();
        stack.pop_back();
        if (b == header) continue;
        for (const BlockId p : fn.blocks[b].preds) {
            if (p < L.inLoop.size() && !L.inLoop[p] && p != header) {
                L.inLoop[p] = 1;
                stack.push_back(p);
            }
        }
    }
    L.inLoop[header] = 1;
    for (const std::uint8_t v : L.inLoop) L.size += v;
    return L;
}

} // namespace

IrPassResult LoopInvariantCodeMotion::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.blocks.size() < 2) continue;
        const opt::DomInfo dom = opt::computeDom(fn);

        // 1) Kumpulkan back-edge (header mendominasi latch); gabung loop
        //    dengan header yang sama.
        std::vector<Loop> loops;
        for (BlockId b = 0; b < fn.blocks.size(); ++b) {
            for (const BlockId s : fn.blocks[b].succs) {
                // Back-edge: header (s) HARUS mendominasi latch (b);
                // dominates(x, y) berarti "y mendominasi x".
                if (s < fn.blocks.size() && s != b && dom.dominates(b, s)) {
                    Loop L = naturalLoop(fn, s, b);
                    bool merged = false;
                    for (Loop& ex : loops) {
                        if (ex.header == L.header) {
                            for (std::uint32_t i = 0; i < L.inLoop.size();
                                 ++i) {
                                if (L.inLoop[i] && !ex.inLoop[i]) {
                                    ex.inLoop[i] = 1;
                                    ex.size += 1;
                                }
                            }
                            merged = true;
                            break;
                        }
                    }
                    if (!merged) loops.push_back(std::move(L));
                }
            }
        }
        if (getenv("RHEINC_DEBUG_LICM") != nullptr) {
            fprintf(stderr, "[licm] func=%u blocks=%zu loops=%zu\n",
                    f, fn.blocks.size(), loops.size());
        }
        if (loops.empty()) continue;
        // Innermost-dulu: urutkan ukuran ascending.
        std::sort(loops.begin(), loops.end(),
                  [](const Loop& a, const Loop& b) { return a.size < b.size; });

        // Cache def->block sekali per fungsi: instruksi -> block yang
        // mengandungnya di urutan (atau -1 bila tak ter-urut).
        const std::uint32_t nAllInstr =
            static_cast<std::uint32_t>(fn.instrs.size());
        std::vector<std::int32_t> defBlock(nAllInstr, -1);
        for (BlockId db = 0; db < fn.blocks.size(); ++db) {
            for (const InstrId id : fn.blocks[db].insts) {
                if (id < nAllInstr) defBlock[id] = db;
            }
        }

        bool fnChanged = false;
        for (const Loop& L : loops) {
            // 2) Preheader: pred luar tunggal header, succ-nya hanya header.
            BlockId pre = InvalidBlock;
            {
                std::vector<BlockId> outside;
                for (const BlockId p : fn.blocks[L.header].preds) {
                    if (p < L.inLoop.size() && !L.inLoop[p]) {
                        outside.push_back(p);
                    }
                }
                if (outside.size() == 1) {
                    const IrBlock& pb = fn.blocks[outside[0]];
                    if (pb.succs.size() == 1 && pb.succs[0] == L.header) {
                        pre = outside[0];
                    }
                }
            }
            if (getenv("RHEINC_DEBUG_LICM") != nullptr) {
                std::uint32_t inv = 0;
                for (BlockId bb = 0; bb < fn.blocks.size(); ++bb) {
                    if (!L.inLoop[bb]) continue;
                    for (const InstrId id : fn.blocks[bb].insts)
                        if (id < fn.instrs.size()) ++inv;
                }
                fprintf(stderr, "[licm]  loop header=b%u size=%u pre=%d instrs=%u\n",
                        L.header, L.size, (int)pre, inv);
            }
            if (pre == InvalidBlock) continue;  // documented skip

            // 3) Exit loop: node loop dengan succ di luar loop.
            std::vector<BlockId> exits;
            for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                if (!L.inLoop[b]) continue;
                for (const BlockId s : fn.blocks[b].succs) {
                    if (s >= L.inLoop.size() || !L.inLoop[s]) {
                        exits.push_back(b);
                        break;
                    }
                }
            }

            // 4) Invarian (fixpoint): indeks per InstrId.
            const std::uint32_t nInstr =
                static_cast<std::uint32_t>(fn.instrs.size());
            std::vector<std::uint8_t> invariant(nInstr, 0);
            std::vector<std::uint8_t> decided(nInstr, 0);
            bool progress = true;
            while (progress) {
                progress = false;
                for (BlockId b = 0; b < fn.blocks.size(); ++b) {
                    if (!L.inLoop[b]) continue;
                    const IrBlock& blk = fn.blocks[b];
                    for (const InstrId iid : blk.insts) {
                        if (iid >= nInstr || decided[iid]) continue;
                        const IrInstr& instr = fn.instrs[iid];
                        // Konstanta tidak perlu diangkat: nilainya sudah
                        // inline (ImmV) dan tidak membawa penghematan.
                        if (instr.resultType == InvalidType ||
                            instr.op == IrOpcode::ConstInt ||
                            instr.op == IrOpcode::ConstBool ||
                            instr.op == IrOpcode::ConstFloat ||
                            instr.op == IrOpcode::ConstString ||
                            !irOpIsPureForDce(instr.op)) {
                            decided[iid] = 1;
                            continue;
                        }
                        bool inv = true;
                        for (const IrOperand& op : instr.operands) {
                            if (op.kind == IrOperandKind::ValueV &&
                                op.id < fn.values.size()) {
                                const IrValue& v = fn.values[op.id];
                                if (v.kind == IrValue::Kind::Param) continue;
                                if (v.kind == IrValue::Kind::Instr &&
                                    // defBlock adalah snapshot awal fungsi;
                                    // instruksi lebih baru (hasil hoist
                                    // loop sebelumnya, id >= defBlock.size())
                                    // didefinisikan di preheader = DI LUAR
                                    // loop -> invarian. Guard WAJIB pakai
                                    // defBlock.size(), bukan nInstr (yang
                                    // tumbuh per loop) -- akses di luar itu
                                    // OOB (bug non-determinisme + dominance).
                                    v.payload < defBlock.size()) {
                                    const InstrId defId = v.payload;
                                    const std::int32_t db = defBlock[defId];
                                    if (db >= 0 &&
                                        L.inLoop[static_cast<BlockId>(db)]) {
                                        if (invariant[defId]) continue;
                                        inv = false;
                                        break;
                                    }
                                    continue;  // def di luar loop
                                }
                                inv = false;
                                break;
                            }
                            // ImmV/ConstantV/BlockV/TypeV: konstan -> invarian
                        }
                        decided[iid] = 1;
                        if (inv) {
                            invariant[iid] = 1;
                            progress = true;
                        }
                    }
                }
            }

            // 5) Hoist: kandidat = invarian DAN (tak-bisa-trap ATAU block
            //    mendominasi semua exit).
            IrBlock& preBlk = fn.blocks[pre];
            const std::size_t preTermPos =
                preBlk.insts.empty() ? 0 : preBlk.insts.size() - 1;
            for (BlockId b = 0; b < fn.blocks.size() && !result.ok == false;
                 ++b) {
                if (!L.inLoop[b]) continue;
                const IrBlock& blk = fn.blocks[b];
                // Syarat aman untuk op yang bisa trap (Div/Mod/dst):
                // instruksi harus dieksekusi setiap kali loop dieksekusi.
                // Cek: block instruksi mendominasi SEMUA SUMBER exit loop
                // (block dalam loop dengan edge keluar) — dominates(e, b)
                // berarti "b mendominasi e".
                bool dominatesAllExits = true;
                for (const BlockId e : exits) {
                    if (!dom.dominates(e, b)) {
                        dominatesAllExits = false;
                        break;
                    }
                }
                for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                    const InstrId iid = blk.insts[pos];
                    if (iid >= nInstr || !invariant[iid]) continue;
                    const IrInstr instr = fn.instrs[iid];  // snapshot
                    if (instr.op == IrOpcode::ConstInt ||
                        instr.op == IrOpcode::ConstBool ||
                        instr.op == IrOpcode::ConstFloat ||
                        instr.op == IrOpcode::ConstString) continue;
                    // Ketersediaan operand di preheader: setiap operand
                    // ValueV harus didefinisikan DI LUAR loop, atau def-nya
                    // SUDAH di-hoist (id baru, tak ada di snapshot defBlock).
                    // Tanpa cek ini, konsumen dari nilai invarian yang tidak
                    // terangkat (mis. Mod mayTrap) tetap diangkat -> SSA
                    // rusak (def tak mendominasi use -- bug bilangan_prima).
                    {
                        bool avail = true;
                        for (const IrOperand& o : instr.operands) {
                            if (o.kind != IrOperandKind::ValueV ||
                                o.id >= fn.values.size()) continue;
                            const IrValue& ov = fn.values[o.id];
                            if (ov.kind != IrValue::Kind::Instr) continue;
                            if (ov.payload < defBlock.size() &&
                                defBlock[ov.payload] >= 0 &&
                                L.inLoop[static_cast<BlockId>(
                                    defBlock[ov.payload])]) {
                                avail = false;
                                break;
                            }
                        }
                        if (!avail) continue;
                    }
                    if (mayTrapOnExtremes(instr.op) &&
                        !dominatesAllExits) {
                        continue;
                    }
                    // Cari value milik instruksi ini.
                    ValueId vSelf = InvalidValue;
                    for (std::uint32_t v = 0; v < fn.values.size(); ++v) {
                        if (fn.values[v].kind == IrValue::Kind::Instr &&
                            fn.values[v].payload == iid) {
                            vSelf = v;
                            break;
                        }
                    }
                    if (vSelf == InvalidValue) continue;
                    // Sisipkan salinan di ujung preheader (sebelum
                    // terminator). Dependensi antar kandidat terjaga karena
                    // iterasi mengikuti urutan program (def sebelum use).
                    ValueId vNew = InvalidValue;
                    if (getenv("RHEINC_DEBUG_LICM")) {
                        fprintf(stderr, "[licm] hoist func=%u pre=b%u pos=%zu iid=%u op=%d -> ",
                                f, pre, preTermPos, iid, (int)instr.op);
                    }
                    if (!editor.insertInstrBefore(f, pre, preTermPos, instr,
                                                  vNew).ok) {
                        result.ok = false;
                        return result;
                    }
                    if (getenv("RHEINC_DEBUG_LICM")) {
                        fprintf(stderr, "newV=%u preSize=%zu\n", vNew,
                                fn.blocks[pre].insts.size());
                    }
                    // Alihkan semua USE lama -> baru (seluruh fungsi).
                    for (BlockId ub = 0; ub < fn.blocks.size(); ++ub) {
                        IrBlock& ublk = fn.blocks[ub];
                        for (std::size_t upos = 0;
                             upos < ublk.insts.size(); ++upos) {
                            const InstrId uiid = ublk.insts[upos];
                            if (uiid >= fn.instrs.size()) continue;
                            const std::vector<IrOperand> ops =
                                fn.instrs[uiid].operands;
                            for (std::size_t oi = 0; oi < ops.size(); ++oi) {
                                if (ops[oi].kind == IrOperandKind::ValueV &&
                                    ops[oi].id == vSelf) {
                                    if (!editor.replaceOperandAt(
                                            f, ub, upos, oi,
                                            IrOperand::value(vNew)).ok) {
                                        result.ok = false;
                                        return result;
                                    }
                                    result.folded += 1;
                                }
                            }
                        }
                    }
                    fnChanged = true;
                    invariant[iid] = 0;  // jangan hoist ulang
                }
            }
        }

        // 6) Sapu instruksi mati hasil pengangkatan.
        if (fnChanged) {
            std::uint32_t removedDead = 0;
            if (!editor.removeDeadInstrs(f, removedDead).ok) {
                result.ok = false;
                return result;
            }
            result.changed = true;
            result.folded += removedDead;
        }
    }
    return result;
}

} // namespace rhc
