#include "RhOptimizer/LoopUnroll.h"

#include "RhIR/IrDomAnalysis.h"
#include "RhIR/IrEditor.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace rhc {

namespace {

struct CarriedPhi {
    ValueId phi = InvalidValue;
    IrOperand initOp;          // operand pair dari pre
    ValueId upd = InvalidValue;
};

std::vector<std::int32_t> buildDefBlock(const IrFunction& fn) {
    std::vector<std::int32_t> defBlock(fn.instrs.size(), -1);
    for (BlockId b = 0; b < fn.blocks.size(); ++b) {
        for (const InstrId id : fn.blocks[b].insts) {
            if (id < fn.instrs.size()) defBlock[id] = b;
        }
    }
    return defBlock;
}

ValueId valueOfInstr(const IrFunction& fn, InstrId iid) {
    for (std::uint32_t v = 0; v < fn.values.size(); ++v) {
        if (fn.values[v].kind == IrValue::Kind::Instr &&
            fn.values[v].payload == iid) {
            return v;
        }
    }
    return InvalidValue;
}

bool constImm(const IrFunction& fn, const IrOperand& op, std::uint64_t& out) {
    if (op.kind == IrOperandKind::ImmV) { out = op.id; return true; }
    if (op.kind == IrOperandKind::ValueV && op.id < fn.values.size()) {
        const IrValue& v = fn.values[op.id];
        if (v.kind == IrValue::Kind::Instr && v.payload < fn.instrs.size()) {
            const IrInstr& d = fn.instrs[v.payload];
            // ConstInt 64-bit: 1-2 ImmV lo/hi (D-4); ConstBool 1 ImmV.
            if (d.op == IrOpcode::ConstInt) {
                return decodeConstInt(d, out);
            }
            if (d.op == IrOpcode::ConstBool && d.operands.size() == 1 &&
                d.operands[0].kind == IrOperandKind::ImmV) {
                out = d.operands[0].id;
                return true;
            }
        }
    }
    return false;
}

} // namespace

IrPassResult LoopUnroll::run(IrModule& module) {
    IrPassResult result;
    IrEditor editor(module);

    for (FuncId f = 0; f < module.functions.size(); ++f) {
        IrFunction& fn = module.functions[f];
        if (fn.blocks.size() < 3) continue;
        const DomInfo dom = computeDom(fn);
        const std::uint32_t nBlocks = fn.blocks.size();
        const std::vector<std::int32_t> defBlock = buildDefBlock(fn);

        // ---- deteksi loop ----
        std::vector<std::vector<std::uint8_t>> loops;
        std::vector<BlockId> headers;
        for (BlockId l = 0; l < nBlocks; ++l) {
            for (const BlockId s : fn.blocks[l].succs) {
                if (s >= nBlocks || s == l || !dom.dominates(l, s)) continue;
                bool merged = false;
                for (std::size_t k = 0; k < loops.size(); ++k) {
                    if (headers[k] != s) continue;
                    std::vector<BlockId> stack{l};
                    while (!stack.empty()) {
                        const BlockId b = stack.back();
                        stack.pop_back();
                        if (b == s) continue;
                        if (!loops[k][b]) {
                            loops[k][b] = 1;
                            for (const BlockId p : fn.blocks[b].preds) {
                                if (p < nBlocks) stack.push_back(p);
                            }
                        }
                    }
                    merged = true;
                    break;
                }
                if (!merged) {
                    std::vector<std::uint8_t> in(nBlocks, 0);
                    in[s] = 1;
                    std::vector<BlockId> stack{l};
                    while (!stack.empty()) {
                        const BlockId b = stack.back();
                        stack.pop_back();
                        if (b == s) continue;
                        if (!in[b]) {
                            in[b] = 1;
                            for (const BlockId p : fn.blocks[b].preds) {
                                if (p < nBlocks) stack.push_back(p);
                            }
                        }
                    }
                    loops.push_back(std::move(in));
                    headers.push_back(s);
                }
            }
        }

        for (std::size_t li = 0; li < loops.size(); ++li) {
            const std::vector<std::uint8_t> inLoop = loops[li];
            const BlockId H = headers[li];

            // ---- validasi bentuk ketat ----
            BlockId pre = InvalidBlock, E = InvalidBlock, B0 = InvalidBlock,
                    L = InvalidBlock;
            std::vector<BlockId> outsidePreds;
            for (const BlockId p : fn.blocks[H].preds) {
                if (p < nBlocks && !inLoop[p]) outsidePreds.push_back(p);
            }
            if (outsidePreds.size() != 1) continue;
            if (fn.blocks[outsidePreds[0]].succs.size() != 1 ||
                fn.blocks[outsidePreds[0]].succs[0] != H) continue;
            pre = outsidePreds[0];

            std::vector<BlockId> outSucc, inSucc;
            for (const BlockId s : fn.blocks[H].succs) {
                if (s >= nBlocks) continue;
                (inLoop[s] ? inSucc : outSucc).push_back(s);
            }
            if (outSucc.size() != 1 || inSucc.size() != 1) continue;
            E = outSucc[0];
            B0 = inSucc[0];

            std::vector<BlockId> latches;
            for (BlockId b = 0; b < nBlocks; ++b) {
                if (!inLoop[b] || b == H) continue;
                for (const BlockId s : fn.blocks[b].succs) {
                    if (s == H) { latches.push_back(b); break; }
                }
            }
            if (latches.size() != 1) continue;
            L = latches[0];
            if (fn.blocks[L].succs.size() != 1) continue;


            std::vector<BlockId> chain;
            std::uint8_t walkOk = 1;
            {
                BlockId cur = B0;
                while (true) {
                    if (chain.size() >= nBlocks) { walkOk = 0; break; }
                    chain.push_back(cur);
                    if (cur == L) break;
                    const IrBlock& cb = fn.blocks[cur];
                    if (cb.succs.size() != 1) { walkOk = 0; break; }
                    const BlockId nxt = cb.succs[0];
                    if (nxt >= nBlocks || !inLoop[nxt] || nxt == H) {
                        walkOk = 0; break;
                    }
                    std::uint32_t inner = 0;
                    for (const BlockId p : fn.blocks[nxt].preds) {
                        if (p < nBlocks && inLoop[p]) ++inner;
                    }
                    if (inner != 1) { walkOk = 0; break; }
                    cur = nxt;
                }
            }
            if (!walkOk) continue;
            std::uint32_t memberCount = 0;
            for (const std::uint8_t v : inLoop) memberCount += v;
            if (chain.size() != memberCount - 1) continue;

            const InstrId termId = fn.blocks[H].insts.empty()
                                       ? InvalidInstr
                                       : fn.blocks[H].insts.back();
            // Routing: fullEligible = trip count statis valid (1..16);
            // loop berbentuk ketat tanpa trip count -> partial (peeling).
            bool fullEligible = false;
            std::uint32_t N = 0;
            ValueId cmpV = InvalidValue;
            ValueId indPhi = InvalidValue;
            if (termId != InvalidInstr && termId < fn.instrs.size()) {
                const IrInstr term = fn.instrs[termId];  // snapshot
                if (term.op == IrOpcode::CondBr &&
                    term.operands.size() == 3 &&
                    term.operands[0].kind == IrOperandKind::ValueV &&
                    term.operands[1].kind == IrOperandKind::BlockV &&
                    term.operands[2].kind == IrOperandKind::BlockV &&
                    term.operands[1].id == static_cast<std::uint32_t>(B0) &&
                    term.operands[2].id == static_cast<std::uint32_t>(E)) {
                    cmpV = term.operands[0].id;
                    bool cmpOk = false;
                    IrInstr cmpI;  // snapshot bila valid
                    if (cmpV < fn.values.size()) {
                        const IrValue& cmpVal = fn.values[cmpV];
                        if (cmpVal.kind == IrValue::Kind::Instr &&
                            cmpVal.payload < fn.instrs.size()) {
                            cmpI = fn.instrs[cmpVal.payload];
                            cmpOk = cmpI.op == IrOpcode::CmpLt &&
                                    cmpI.operands.size() == 2;
                        }
                    }
                    if (cmpOk) {
                        std::uint8_t tripOk = 0;
                        for (std::uint32_t side = 0; side < 2; ++side) {
                            const IrOperand& po = cmpI.operands[side];
                            const IrOperand& ko = cmpI.operands[1 - side];
                            if (po.kind != IrOperandKind::ValueV) continue;
                            const IrValue& pv = fn.values[po.id];
                            if (pv.kind != IrValue::Kind::Instr ||
                                pv.payload >= fn.instrs.size()) continue;
                            if (defBlock[pv.payload] !=
                                static_cast<std::int32_t>(H)) continue;
                            if (fn.instrs[pv.payload].op != IrOpcode::Phi) continue;
                            std::uint64_t nC = 0;
                            if (!constImm(fn, ko, nC)) continue;
                            indPhi = po.id;
                            N = nC;
                            tripOk = 1;
                            break;
                        }
                        if (tripOk && N >= 1 && N <= 16) fullEligible = true;
                    }
                }
            }

            std::vector<CarriedPhi> phis;
            std::vector<InstrId> hInstrs;
            std::uint8_t phiOk = 1;
            for (const InstrId iid : fn.blocks[H].insts) {
                if (iid >= fn.instrs.size()) continue;
                const IrInstr& ins = fn.instrs[iid];
                if (ins.op != IrOpcode::Phi) {
                    if (iid != termId) hInstrs.push_back(iid);
                    continue;
                }
                if (ins.operands.size() != 4) { phiOk = 0; break; }
                CarriedPhi cp;
                cp.phi = valueOfInstr(fn, iid);
                if (cp.phi == InvalidValue) { phiOk = 0; break; }
                bool gotInit = false, gotUpd = false;
                for (std::uint32_t i = 0; i + 1 < ins.operands.size(); i += 2) {
                    if (ins.operands[i].kind != IrOperandKind::BlockV) continue;
                    const BlockId src = ins.operands[i].id;
                    const IrOperand& val = ins.operands[i + 1];
                    if (src == pre) {
                        cp.initOp = val;
                        gotInit = true;
                    } else if (src == L) {
                        if (val.kind != IrOperandKind::ValueV) { phiOk = 0; break; }
                        cp.upd = val.id;
                        gotUpd = true;
                    }
                }
                if (!gotInit || !gotUpd) { phiOk = 0; break; }

                const IrValue& uv = fn.values[cp.upd];
                if (uv.kind != IrValue::Kind::Instr ||
                    uv.payload >= defBlock.size() ||
                    defBlock[uv.payload] < 0 ||
                    !inLoop[static_cast<BlockId>(defBlock[uv.payload])] ||
                    defBlock[uv.payload] == static_cast<std::int32_t>(H)) {
                    phiOk = 0; break;
                }
                if (cp.initOp.kind == IrOperandKind::ValueV &&
                    cp.initOp.id < fn.values.size()) {
                    const IrValue& iv = fn.values[cp.initOp.id];
                    if (iv.kind == IrValue::Kind::Instr &&
                        iv.payload < defBlock.size() && defBlock[iv.payload] >= 0 &&
                        inLoop[static_cast<BlockId>(defBlock[iv.payload])]) {
                        phiOk = 0; break;
                    }
                }
                phis.push_back(cp);
            }
            if (!phiOk || phis.empty()) continue;

            std::uint8_t indOk = 0;
            for (const CarriedPhi& cp : phis) {
                if (cp.phi != indPhi) continue;
                std::uint64_t z = 1;
                if (!constImm(fn, cp.initOp, z) || z != 0) break;
                const IrValue& uv = fn.values[cp.upd];
                const IrInstr& u = fn.instrs[uv.payload];
                if (u.op != IrOpcode::Add || u.operands.size() != 2) break;
                bool hasPhi = false, hasOne = false;
                for (const IrOperand& o : u.operands) {
                    if (o.kind == IrOperandKind::ValueV && o.id == cp.phi) hasPhi = true;
                    std::uint64_t one = 0;
                    if (constImm(fn, o, one) && one == 1) hasOne = true;
                }
                if (!hasPhi || !hasOne) break;
                indOk = 1;
            }
            if (!indOk) fullEligible = false;

            std::uint32_t bodyInstrs = hInstrs.size();
            for (const BlockId b : chain) bodyInstrs += fn.blocks[b].insts.size();
            if (fullEligible && bodyInstrs * N > 512) fullEligible = false;

            // ============ PARTIAL UNROLLING (PEELING K=2) ============
            // Loop berbentuk ketat tanpa trip count statis: peel 2 iterasi
            // pertama keluar dari loop. Setiap clone head mengecek kondisi
            // (CondBr mirip header); bila false -> exit E dengan nilai
            // carried yang benar. Loop asli tetap hidup; phi H diperbarui
            // (jalur masuk dari latch clone terakhir), exit phi di E
            // dilengkapi pasangan incoming per clone head.
            if (!fullEligible) {
                const std::uint32_t K = 2;
                std::unordered_map<ValueId, IrOperand> phiInit;
                std::unordered_map<ValueId, ValueId> phiUpd;
                for (const CarriedPhi& cp : phis) {
                    phiInit[cp.phi] = cp.initOp;
                    phiUpd[cp.phi] = cp.upd;
                }
                std::uint32_t bodyP = hInstrs.size();
                for (const BlockId b : chain) {
                    bodyP += fn.blocks[b].insts.size();
                }
                const bool cmpInH =
                    cmpV != InvalidValue && cmpV < fn.values.size() &&
                    fn.values[cmpV].kind == IrValue::Kind::Instr &&
                    fn.values[cmpV].payload < defBlock.size() &&
                    defBlock[fn.values[cmpV].payload] ==
                        static_cast<std::int32_t>(H);
                // Eligibilitas exit phi: pair [H, vH] yang vH-nya def di
                // CHAIN (bukan phi H / def H / eksternal) unsafe untuk peel
                // jalur hc_0 (value belum terdefinisi) -> skip loop ini.
                bool exitPhiSafe = true;
                for (const InstrId iid : fn.blocks[E].insts) {
                    if (iid >= fn.instrs.size() ||
                        fn.instrs[iid].op != IrOpcode::Phi) continue;
                    const IrInstr& pin = fn.instrs[iid];
                    for (std::uint32_t i = 0; i + 1 < pin.operands.size(); i += 2) {
                        if (pin.operands[i].kind != IrOperandKind::BlockV ||
                            pin.operands[i].id != static_cast<std::uint32_t>(H)) continue;
                        const IrOperand& vH = pin.operands[i + 1];
                        if (vH.kind != IrOperandKind::ValueV ||
                            vH.id >= fn.values.size()) continue;
                        const IrValue& vv = fn.values[vH.id];
                        if (vv.kind != IrValue::Kind::Instr ||
                            vv.payload >= defBlock.size()) continue;
                        const std::int32_t db = defBlock[vv.payload];
                        if (db >= 0 && db != static_cast<std::int32_t>(H) &&
                            inLoop[static_cast<BlockId>(db)]) {
                            exitPhiSafe = false;
                        }
                    }
                }
                if (termId != InvalidInstr && cmpInH && exitPhiSafe &&
                    fn.instrs.size() + K * bodyP <= 2048) {
                    const std::vector<BlockId> chainCopy = chain;
                    const std::vector<InstrId> hCopy = hInstrs;
                    std::vector<std::unordered_map<ValueId, ValueId>> iterMap(K);
                    std::vector<BlockId> headClone(K), latchClone(K);
                    bool failed = false;
                    for (std::uint32_t c = 0; c < K && !failed; ++c) {
                        auto remapOp = [&](const IrOperand& op) -> IrOperand {
                            if (op.kind != IrOperandKind::ValueV) return op;
                            const ValueId vid = op.id;
                            if (phiInit.count(vid)) {
                                if (c == 0) return phiInit[vid];
                                const auto it = iterMap[c - 1].find(phiUpd[vid]);
                                if (it == iterMap[c - 1].end()) { failed = true; return op; }
                                return IrOperand::value(it->second);
                            }
                            const auto itc = iterMap[c].find(vid);
                            if (itc != iterMap[c].end()) return IrOperand::value(itc->second);
                            return op;  // eksternal / clone sesi ini
                        };
                        BlockId hc = InvalidBlock;
                        if (!editor.appendBlock(f, hc).ok) { failed = true; break; }
                        headClone[c] = hc;
                        // [FIX] Clone dua-pass: urutan instruksi di blok
                        // bisa terserah (merge simplify-cfg), sehingga
                        // remap satu-pass melewatkan rujukan maju. Pass 1:
                        // klon MENTAH + daftarkan iterMap; pass 2: remap
                        // operand via replaceOperandAt.
                        std::vector<std::size_t> headArity;
                        for (const InstrId iid : hCopy) {
                            if (fn.instrs[iid].op == IrOpcode::Phi) { failed = true; break; }
                            IrInstr ci = fn.instrs[iid];  // mentah, belum remap
                            ValueId nv = InvalidValue;
                            if (!editor.insertInstrBefore(f, hc, fn.blocks[hc].insts.size(), ci, nv).ok) { failed = true; break; }
                            const ValueId ov = valueOfInstr(fn, iid);
                            if (ov != InvalidValue) iterMap[c][ov] = nv;
                            headArity.push_back(fn.instrs[fn.blocks[hc].insts.back()].operands.size());
                        }
                        if (failed) break;
                        for (std::size_t hp = 0; hp < headArity.size() && !failed; ++hp) {
                            for (std::size_t oi = 0; oi < headArity[hp]; ++oi) {
                                const IrOperand cur = fn.instrs[fn.blocks[hc].insts[hp]].operands[oi];
                                const IrOperand mo = remapOp(cur);
                                if (failed) break;
                                if (mo.kind == cur.kind && mo.id == cur.id) continue;
                                if (!editor.replaceOperandAt(f, hc, hp, oi, mo).ok) { failed = true; break; }
                            }
                        }
                        if (failed) break;
                        std::vector<BlockId> chainClone(chainCopy.size());
                        for (std::size_t bi = 0; bi < chainCopy.size() && !failed; ++bi) {
                            const BlockId b = chainCopy[bi];
                            BlockId bc = InvalidBlock;
                            if (!editor.appendBlock(f, bc).ok) { failed = true; break; }
                            chainClone[bi] = bc;
                            IrBlock& nblk = fn.blocks[bc];
                            // [FIX] Clone dua-pass (lihat headClone di
                            // atas). Phi 1-pasangan (latch phi yang ikut
                            // ter-merge ke chain block) dilipat: pemakaian
                            // merujuk langsung ke nilai pasangannya.
                            std::vector<std::pair<ValueId, ValueId>> foldPairs;
                            std::vector<std::size_t> chainPos;
                            std::vector<std::size_t> chainArity;
                            for (std::size_t ii = 0; ii + 1 < fn.blocks[b].insts.size(); ++ii) {
                                const InstrId iid = fn.blocks[b].insts[ii];
                                const IrInstr& srcI = fn.instrs[iid];
                                const ValueId ov = valueOfInstr(fn, iid);
                                if (srcI.op == IrOpcode::Phi) {
                                    if (srcI.operands.size() != 2 ||
                                        srcI.operands[0].kind != IrOperandKind::BlockV ||
                                        srcI.operands[1].kind != IrOperandKind::ValueV) {
                                        failed = true; break;
                                    }
                                    foldPairs.push_back({ov, srcI.operands[1].id});
                                    continue;
                                }
                                IrInstr ci = srcI;  // mentah, belum remap
                                ValueId nv = InvalidValue;
                                if (!editor.insertInstrBefore(f, bc, nblk.insts.size(), ci, nv).ok) { failed = true; break; }
                                if (ov != InvalidValue) iterMap[c][ov] = nv;
                                chainPos.push_back(nblk.insts.size() - 1);
                                chainArity.push_back(fn.instrs[nblk.insts.back()].operands.size());
                            }
                            for (const auto& [fov, fval] : foldPairs) {
                                if (fov == InvalidValue) continue;
                                const IrOperand m = remapOp(IrOperand::value(fval));
                                if (failed) break;
                                iterMap[c][fov] = m.id;
                            }
                            if (failed) break;
                            for (std::size_t cp2 = 0; cp2 < chainPos.size() && !failed; ++cp2) {
                                for (std::size_t oi = 0; oi < chainArity[cp2]; ++oi) {
                                    const IrOperand cur = fn.instrs[fn.blocks[bc].insts[chainPos[cp2]]].operands[oi];
                                    const IrOperand mo = remapOp(cur);
                                    if (failed) break;
                                    if (mo.kind == cur.kind && mo.id == cur.id) continue;
                                    if (!editor.replaceOperandAt(f, bc, chainPos[cp2], oi, mo).ok) { failed = true; break; }
                                }
                            }
                        }
                        if (failed) break;
                        // terminator clone head: CondBr(cmpClone, chain, E)
                        const auto cmpIt = iterMap[c].find(cmpV);
                        if (cmpIt == iterMap[c].end()) { failed = true; break; }
                        IrBlock& hb = fn.blocks[hc];
                        hb.preds = {c == 0 ? pre : latchClone[c - 1]};
                        hb.succs = {chainClone[0], E};
                        IrInstr cbr;
                        cbr.op = IrOpcode::CondBr;
                        cbr.operands = {IrOperand::value(cmpIt->second),
                                        IrOperand::block(chainClone[0]),
                                        IrOperand::block(E)};
                        ValueId dummy = InvalidValue;
                        if (!editor.insertInstrBefore(f, hc, hb.insts.size(), cbr, dummy).ok) { failed = true; break; }
                        for (std::size_t bi = 0; bi < chainClone.size(); ++bi) {
                            IrBlock& nb = fn.blocks[chainClone[bi]];
                            nb.preds = {bi == 0 ? hc : chainClone[bi - 1]};
                            BlockId tgt;
                            if (bi + 1 < chainClone.size()) tgt = chainClone[bi + 1];
                            else tgt = (c + 1 < K) ? headClone[c + 1] : H;  // K=2: c0->hc1, c1->H
                            nb.succs = {tgt};
                            IrInstr br;
                            br.op = IrOpcode::Br;
                            br.operands = {IrOperand::block(tgt)};
                            ValueId dv = InvalidValue;
                            if (!editor.insertInstrBefore(f, chainClone[bi], nb.insts.size(), br, dv).ok) { failed = true; break; }
                            if (bi + 1 == chainClone.size()) latchClone[c] = chainClone[bi];
                        }
                        if (failed) break;
                    }
                    if (failed) { result.ok = false; return result; }

                    // Patch tunda: terminator latch clone c<K-1 -> headClone[c+1].
                    // headClone[c+1] baru ada setelah SEMUA iterasi diklon,
                    // jadi wiring ini tak bisa dipasang saat kloning (bug
                    // latchClone[0] menunjuk pre ditemukan verifyEachPass).
                    for (std::uint32_t c = 0; c + 1 < K; ++c) {
                        IrBlock& lb = fn.blocks[latchClone[c]];
                        const InstrId tid = lb.insts.back();
                        IrInstr t = fn.instrs[tid];  // snapshot
                        t.operands = {IrOperand::block(headClone[c + 1])};
                        if (!editor.replaceInstr(f, latchClone[c],
                                                 lb.insts.size() - 1, t).ok) {
                            result.ok = false; return result;
                        }
                        lb.succs = {headClone[c + 1]};
                    }

                    // ---- rewiring ----
                    // pre -> headClone[0]
                    {
                        IrBlock& pb = fn.blocks[pre];
                        IrInstr t = fn.instrs[pb.insts.back()];
                        t.op = IrOpcode::Br;
                        t.operands = {IrOperand::block(headClone[0])};
                        if (!editor.replaceInstr(f, pre, pb.insts.size() - 1, t).ok) { result.ok = false; return result; }
                        pb.succs = {headClone[0]};
                    }
                    // H.preds: pre -> latchClone[K-1]
                    for (BlockId& p : fn.blocks[H].preds) {
                        if (p == pre) p = latchClone[K - 1];
                    }
                    // Remap operand non-phi di E yang merujuk phi H / def H
                    // -> nilai "setelah loop" (cloneUpd[K-1] / clone head[K-1]).
                    // Tanpa ini, use non-phi seperti Ret ke phi H menjadi
                    // invalid pasca-peel (jalur hc_0 -> E melewati H).
                    {
                        IrBlock& eb = fn.blocks[E];
                        for (std::size_t pos = 0; pos < eb.insts.size(); ++pos) {
                            const InstrId iid = eb.insts[pos];
                            if (iid >= fn.instrs.size()) continue;
                            if (fn.instrs[iid].op == IrOpcode::Phi) continue;
                            const std::vector<IrOperand> ops = fn.instrs[iid].operands;
                            for (std::size_t oi = 0; oi < ops.size(); ++oi) {
                                if (ops[oi].kind != IrOperandKind::ValueV) continue;
                                const ValueId vid = ops[oi].id;
                                IrOperand rn = ops[oi];
                                const auto itP = phiUpd.find(vid);
                                if (itP != phiUpd.end()) {
                                    const auto it = iterMap[K - 1].find(itP->second);
                                    if (it != iterMap[K - 1].end()) rn = IrOperand::value(it->second);
                                } else if (vid < fn.values.size()) {
                                    const IrValue& vv = fn.values[vid];
                                    if (vv.kind == IrValue::Kind::Instr &&
                                        vv.payload < defBlock.size() &&
                                        defBlock[vv.payload] == static_cast<std::int32_t>(H)) {
                                        const auto it = iterMap[K - 1].find(vid);
                                        if (it != iterMap[K - 1].end()) rn = IrOperand::value(it->second);
                                    }
                                }
                                if (rn.id != ops[oi].id) {
                                    if (!editor.replaceOperandAt(f, E, pos, oi, rn).ok) {
                                        result.ok = false; return result;
                                    }
                                }
                            }
                        }
                    }
                    // Phi exit BARU untuk operand non-phi di E yang merujuk
                    // cloneUpd (def di clone latch). Nilai benar bergantung
                    // jalur: hc_0 -> init, hc_1 -> cloneUpd[0], H (loop
                    // penuh) -> cloneUpd[K-1]. Tanpa phi exit, operand
                    // langsung ke cloneUpd[K-1] invalid di jalur hc_0.
                    {
                        IrBlock& eb = fn.blocks[E];
                        for (std::size_t pos = 0; pos < eb.insts.size(); ++pos) {
                            const InstrId iid = eb.insts[pos];
                            if (iid >= fn.instrs.size()) continue;
                            if (fn.instrs[iid].op == IrOpcode::Phi) continue;
                            const std::vector<IrOperand> ops = fn.instrs[iid].operands;
                            for (std::size_t oi = 0; oi < ops.size(); ++oi) {
                                if (ops[oi].kind != IrOperandKind::ValueV) continue;
                                const ValueId vid = ops[oi].id;
                                if (vid >= fn.values.size()) continue;
                                const IrValue& vv = fn.values[vid];
                                if (vv.kind != IrValue::Kind::Instr) continue;
                                // Operand vid didefinisikan di latchClone[c']
                                // (clone upd hasil remap #1) -> wajib phi exit
                                // [hc_0->init, hc_1->cloneUpd_0, H->vid].
                                // Deteksi via scan langsung: defBlock adalah
                                // snapshot awal fungsi -- instruksi clone
                                // (id baru) melampaui batasnya (OOB), maka
                                // gerbang defBlock.size() DIHAPUS (vv.payload
                                // valid karena < fn.instrs.size() by IR
                                // invariant).
                                std::int32_t cPrime = -1;
                                for (std::uint32_t c = 0; c < K; ++c) {
                                    for (const InstrId lid : fn.blocks[latchClone[c]].insts) {
                                        if (valueOfInstr(fn, lid) == vid) {
                                            cPrime = static_cast<std::int32_t>(c);
                                            break;
                                        }
                                    }
                                    if (cPrime >= 0) break;
                                }
                                if (cPrime < 0) continue;
                                ValueId phiHId = InvalidValue;
                                for (const CarriedPhi& cp : phis) {
                                    const auto it = iterMap[static_cast<std::uint32_t>(cPrime)].find(cp.upd);
                                    if (it != iterMap[static_cast<std::uint32_t>(cPrime)].end() &&
                                        it->second == vid) {
                                        phiHId = cp.phi;
                                        break;
                                    }
                                }
                                if (phiHId == InvalidValue) continue;
                                ValueId cloneUpd0 = InvalidValue;
                                {
                                    const auto it = iterMap[0].find(phiUpd[phiHId]);
                                    if (it != iterMap[0].end()) cloneUpd0 = it->second;
                                }
                                if (cloneUpd0 == InvalidValue) continue;
                                const ValueId cloneUpdId = vid;
                                // Operand ini merujuk cloneUpd (def di clone
                                // latch, BUKAN phi H) -> wajib phi exit.
                                // Cek: vid adalah clone dari upd asli (ada di
                                // iterMap[c] untuk suatu c, dengan key = upd
                                // asli dari phiUpd).
                                bool isCloneUpd = false;
                                for (const auto& kv : phiUpd) {
                                    for (const auto& im : iterMap[K - 1]) {
                                        if (im.second == vid && im.first == kv.second) {
                                            isCloneUpd = true;
                                            break;
                                        }
                                    }
                                    if (isCloneUpd) break;
                                }
                                if (!isCloneUpd) continue;  // bukan cloneUpd -> lewati
                                // phi exit: tipe dari cloneUpd; instruksi
                                // Phi dibuat via editor.insertInstrBefore
                                // (pass tak punya akses IrBuilder).
                                const TypeId phiType = fn.values[cloneUpdId].type;
                                IrInstr phiI;
                                phiI.op = IrOpcode::Phi;
                                phiI.resultType = phiType;
                                phiI.name = "exit_peel";  // tanda: di-skip oleh rewrite lama
                                std::vector<IrOperand> in;
                                in.push_back(IrOperand::block(headClone[0]));
                                in.push_back(phiInit[phiHId]);
                                for (std::uint32_t c = 1; c < K; ++c) {
                                    const auto it = iterMap[c - 1].find(phiUpd[phiHId]);
                                    in.push_back(IrOperand::block(headClone[c]));
                                    in.push_back(it != iterMap[c - 1].end()
                                                      ? IrOperand::value(it->second)
                                                      : ops[oi]);
                                }
                                in.push_back(IrOperand::block(H));
                                in.push_back(IrOperand::value(cloneUpdId));
                                phiI.operands = std::move(in);
                                ValueId exitPhi = InvalidValue;
                                IrBlock& eb2 = fn.blocks[E];
                                // Phi exit disisipkan SEBELUM terminator (pos
                                // akhir-1); bila E kosong atau hanya terminator,
                                // sisipkan di awal.
                                const std::size_t phiPos = eb2.insts.size() >= 1
                                                               ? eb2.insts.size() - 1
                                                               : 0;
                                if (!editor.insertInstrBefore(f, E, phiPos,
                                                              phiI, exitPhi).ok) {
                                    result.ok = false; return result;
                                }
                                // Operand diganti pada posisi ASLI (setelah
                                // sisipan, pos bergeser +1 bila sisipan <= pos).
                                const std::size_t newPos = phiPos <= pos ? pos + 1 : pos;
                                if (!editor.replaceOperandAt(f, E, newPos, oi,
                                                             IrOperand::value(exitPhi)).ok) {
                                    result.ok = false; return result;
                                }
                            }
                        }
                    }
                    // phi H: pair [pre, *] -> [latchClone[K-1], clone upd K-1]
                    for (const CarriedPhi& cp : phis) {
                        const auto itU = iterMap[K - 1].find(cp.upd);
                        if (itU == iterMap[K - 1].end()) continue;
                        for (std::size_t pos = 0; pos < fn.blocks[H].insts.size(); ++pos) {
                            if (fn.blocks[H].insts[pos] != InvalidInstr &&
                                fn.blocks[H].insts[pos] < fn.instrs.size() &&
                                valueOfInstr(fn, fn.blocks[H].insts[pos]) == cp.phi) {
                                IrInstr& pin = fn.instrs[fn.blocks[H].insts[pos]];
                                for (std::uint32_t i = 0; i + 1 < pin.operands.size(); i += 2) {
                                    if (pin.operands[i].kind == IrOperandKind::BlockV &&
                                        pin.operands[i].id == static_cast<std::uint32_t>(pre)) {
                                        editor.replaceOperandAt(f, H, pos, i, IrOperand::block(latchClone[K - 1]));
                                        editor.replaceOperandAt(f, H, pos, i + 1, IrOperand::value(itU->second));
                                    }
                                }
                                break;
                            }
                        }
                    }
                    // E: preds += semua clone head; exit phi += pasangan [hc_c, carried]
                    {
                        IrBlock& eb = fn.blocks[E];
                        for (const BlockId hc : headClone) eb.preds.push_back(hc);
                        // Nilai carried untuk clone hc_c:
                        //   vH == phi H : c==0 -> initOp; c>=1 -> clone upd c-1
                        //   vH def di H : clone head c (head dievaluasi sebelum
                        //                 kondisi -> tersedia di semua c)
                        //   eksternal   : vH apa adanya
                        auto peelExitVal = [&](const IrOperand& vOp,
                                               std::uint32_t c) -> IrOperand {
                            if (vOp.kind != IrOperandKind::ValueV) return vOp;
                            const ValueId vid = vOp.id;
                            const auto itPhi = phiUpd.find(vid);
                            if (itPhi != phiUpd.end()) {
                                if (c == 0) return phiInit[vid];
                                const auto it = iterMap[c - 1].find(itPhi->second);
                                return it != iterMap[c - 1].end()
                                           ? IrOperand::value(it->second) : vOp;
                            }
                            if (vid < fn.values.size()) {
                                const IrValue& vv = fn.values[vid];
                                if (vv.kind == IrValue::Kind::Instr &&
                                    vv.payload < defBlock.size() &&
                                    defBlock[vv.payload] ==
                                        static_cast<std::int32_t>(H)) {
                                    const auto it = iterMap[c].find(vid);
                                    return it != iterMap[c].end()
                                               ? IrOperand::value(it->second) : vOp;
                                }
                            }
                            return vOp;
                        };
                        // Phi exit: pasangan [H, *] LAMA tidak sah pasca-peel
                        // (H tak lagi mendominasi jalur hc_0 -> E). Rewrite:
                        // iterasi mundur pasangan [H, *] -> [target baru,
                        // nilai baru] via replaceOperandAt (replaceInstr
                        // menolak perubahan resultType). Nilai "setelah
                        // loop" untuk vH == phi H = cloneUpd[K-1] (wajar
                        // bila loop berjalan penuh; definisi formal butuh
                        // trip count -- dokumentasi dibatasi ke bentuk ini).
                        {
                            IrBlock& eb2 = fn.blocks[E];
                            for (const InstrId iid : eb2.insts) {
                                if (iid >= fn.instrs.size() ||
                                    fn.instrs[iid].op != IrOpcode::Phi) continue;
                                if (fn.instrs[iid].name == "exit_peel") continue;
                                std::size_t pos = 0;
                                while (pos < eb2.insts.size() && eb2.insts[pos] != iid) ++pos;
                                if (pos == eb2.insts.size()) continue;
                                IrOperand vH;
                                bool haveVH = false;
                                {
                                    const IrInstr& pin = fn.instrs[iid];
                                    for (std::uint32_t i = 0; i + 1 < pin.operands.size(); i += 2) {
                                        if (pin.operands[i].kind == IrOperandKind::BlockV &&
                                            pin.operands[i].id == static_cast<std::uint32_t>(H)) {
                                            vH = pin.operands[i + 1];
                                            haveVH = true;
                                        }
                                    }
                                }
                                if (!haveVH) continue;
                                IrOperand afterVal = vH;
                                if (vH.kind == IrOperandKind::ValueV &&
                                    phiUpd.count(vH.id)) {
                                    const auto it = iterMap[K - 1].find(phiUpd[vH.id]);
                                    if (it != iterMap[K - 1].end()) {
                                        afterVal = IrOperand::value(it->second);
                                    }
                                }
                                // Rewrite pasangan [H, *] -> [latchClone,
                                // afterVal]. Dua langkah (langkah kedua
                                // menimpa efek snapshot tersangkut dari
                                // langkah pertama yang sempat salah-klasifikasi
                                // lookup iterMap[c] untuk def H non-phi).
                                {
                                    IrInstr pin = fn.instrs[iid];  // snapshot ukuran awal
                                    for (std::int32_t i = static_cast<std::int32_t>(pin.operands.size()) - 2;
                                         i >= 0; i -= 2) {
                                        if (i < 0 || static_cast<std::size_t>(i) + 1 >= fn.instrs[iid].operands.size()) continue;
                                        if (fn.instrs[iid].operands[i].kind == IrOperandKind::BlockV &&
                                            fn.instrs[iid].operands[i].id == static_cast<std::uint32_t>(H)) {
                                            if (!editor.replaceOperandAt(f, E, pos,
                                                    static_cast<std::size_t>(i),
                                                    IrOperand::block(latchClone[K - 1])).ok) {
                                                result.ok = false; return result;
                                            }
                                            if (!editor.replaceOperandAt(f, E, pos,
                                                    static_cast<std::size_t>(i) + 1,
                                                    afterVal).ok) {
                                                result.ok = false; return result;
                                            }
                                        }
                                    }
                                }
                                // Langkah kedua: pastikan tidak ada sisa pasangan
                                // [H, *] (fix snapshot bug di langkah pertama)
                                {
                                    IrInstr pin2 = fn.instrs[iid];  // snapshot baru
                                    for (std::int32_t i = static_cast<std::int32_t>(pin2.operands.size()) - 2;
                                         i >= 0; i -= 2) {
                                        if (i < 0 || static_cast<std::size_t>(i) + 1 >= fn.instrs[iid].operands.size()) continue;
                                        if (fn.instrs[iid].operands[i].kind == IrOperandKind::BlockV &&
                                            fn.instrs[iid].operands[i].id == static_cast<std::uint32_t>(H)) {
                                            if (!editor.replaceOperandAt(f, E, pos,
                                                    static_cast<std::size_t>(i),
                                                    IrOperand::block(latchClone[K - 1])).ok) {
                                                result.ok = false; return result;
                                            }
                                            if (!editor.replaceOperandAt(f, E, pos,
                                                    static_cast<std::size_t>(i) + 1,
                                                    afterVal).ok) {
                                                result.ok = false; return result;
                                            }
                                        }
                                    }
                                }
                                // tambah pasangan peel
                                for (std::uint32_t c = 0; c < K; ++c) {
                                    const IrOperand val = peelExitVal(vH, c);
                                    if (!editor.appendOperandAt(f, E, pos,
                                            IrOperand::block(headClone[c])).ok) {
                                        result.ok = false; return result;
                                    }
                                    if (!editor.appendOperandAt(f, E, pos, val).ok) {
                                        result.ok = false; return result;
                                    }
                                }
                            }
                        }
                    }
                    result.changed = true;
                    result.folded += K;
                }
                continue;  // bentuk sudah terproses (peel atau tidak memenuhi)
            }

            // ================= KLONING (FULL) =================
            const std::vector<BlockId> chainCopy = chain;
            const std::vector<InstrId> hCopy = hInstrs;
            std::unordered_map<ValueId, IrOperand> phiInit;
            std::unordered_map<ValueId, ValueId> phiUpd;
            for (const CarriedPhi& cp : phis) {
                phiInit[cp.phi] = cp.initOp;
                phiUpd[cp.phi] = cp.upd;
            }

            std::vector<std::unordered_map<ValueId, ValueId>> iterMap(N);
            std::vector<BlockId> headClone(N), latchClone(N);
            bool failed = false;

            for (std::uint32_t c = 0; c < N && !failed; ++c) {
                auto remapOp = [&](const IrOperand& op) -> IrOperand {
                    if (op.kind != IrOperandKind::ValueV) return op;
                    const ValueId vid = op.id;
                    if (phiInit.count(vid)) {
                        if (c == 0) return phiInit[vid];
                        const auto it = iterMap[c - 1].find(phiUpd[vid]);
                        if (it == iterMap[c - 1].end()) { failed = true; return op; }
                        return IrOperand::value(it->second);
                    }
                    const auto itc = iterMap[c].find(vid);
                    if (itc != iterMap[c].end()) return IrOperand::value(itc->second);
                    if (vid >= fn.values.size()) return op;
                    const IrValue& v = fn.values[vid];
                    if (v.kind != IrValue::Kind::Instr) return op;
                    const std::int32_t db =
                        v.payload < defBlock.size() ? defBlock[v.payload] : -1;
                    if (db < 0) return op;  // clone sesi ini / tak ter-urut
                    if (db == static_cast<std::int32_t>(H) ||
                        inLoop[static_cast<BlockId>(db)]) {
                        failed = true;  // def loop belum diklon (ref maju?)
                        return op;
                    }
                    return op;  // eksternal
                };

                // 1) block kepala: clone instruksi H non-phi
                BlockId hc = InvalidBlock;
                if (!editor.appendBlock(f, hc).ok) { failed = true; break; }
                headClone[c] = hc;
                for (const InstrId iid : hCopy) {
                    IrInstr ci = fn.instrs[iid];  // snapshot
                    for (IrOperand& o : ci.operands) o = remapOp(o);
                    if (failed) break;
                    ValueId nv = InvalidValue;
                    if (!editor.insertInstrBefore(f, hc, fn.blocks[hc].insts.size(),
                                                  ci, nv).ok) { failed = true; break; }
                    const ValueId ov = valueOfInstr(fn, iid);
                    if (ov != InvalidValue) iterMap[c][ov] = nv;
                }
                if (failed) break;

                // 2) block rantai: clone instruksi body (terminator ditunda)
                std::vector<BlockId> chainClone(chainCopy.size());
                for (std::size_t bi = 0; bi < chainCopy.size() && !failed; ++bi) {
                    const BlockId b = chainCopy[bi];
                    BlockId bc = InvalidBlock;
                    if (!editor.appendBlock(f, bc).ok) { failed = true; break; }
                    chainClone[bi] = bc;
                    IrBlock& nblk = fn.blocks[bc];
                    // clone SEMUA instruksi KECUALI terminator (terakhir);
                    // terminator baru dipasang pada tahap wiring.
                    for (std::size_t ii = 0;
                         ii + 1 < fn.blocks[b].insts.size(); ++ii) {
                        const InstrId iid = fn.blocks[b].insts[ii];
                        IrInstr ci = fn.instrs[iid];  // snapshot
                        for (IrOperand& o : ci.operands) o = remapOp(o);
                        if (failed) break;
                        ValueId nv = InvalidValue;
                        if (!editor.insertInstrBefore(
                                f, bc, nblk.insts.size(), ci, nv).ok) {
                            failed = true; break;
                        }
                        const ValueId ov = valueOfInstr(fn, iid);
                        if (ov != InvalidValue) iterMap[c][ov] = nv;
                    }
                }
                if (failed) break;

                // 3) terminator & wiring (semua target sudah ada)
                IrBlock& hb = fn.blocks[hc];
                hb.preds = {c == 0 ? pre : latchClone[c - 1]};
                hb.succs = {chainClone[0]};
                IrInstr brH;
                brH.op = IrOpcode::Br;
                brH.operands = {IrOperand::block(chainClone[0])};
                ValueId dummy = InvalidValue;
                if (!editor.insertInstrBefore(f, hc, hb.insts.size(), brH, dummy).ok) {
                    failed = true; break;
                }
                for (std::size_t bi = 0; bi < chainClone.size(); ++bi) {
                    IrBlock& nb = fn.blocks[chainClone[bi]];
                    nb.preds = {bi == 0 ? hc : chainClone[bi - 1]};
                    BlockId tgt;
                    if (bi + 1 < chainClone.size()) tgt = chainClone[bi + 1];
                    else if (c + 1 < N) tgt = InvalidBlock;  // headClone[c+1] belum ada
                    else tgt = E;
                    if (c + 1 < N && bi + 1 == chainClone.size()) {
                        tgt = InvalidBlock;  // diperbaiki setelah headClone[c+1]
                    }
                    nb.succs = {tgt};
                    IrInstr br;
                    br.op = IrOpcode::Br;
                    br.operands = {IrOperand::block(tgt)};
                    ValueId dv = InvalidValue;
                    if (!editor.insertInstrBefore(f, chainClone[bi], nb.insts.size(),
                                                  br, dv).ok) { failed = true; break; }
                    if (bi + 1 == chainClone.size()) latchClone[c] = chainClone[bi];
                }
                if (failed) break;
                // latch clone: target headClone[c+1] belum ada; tunda via
                // patch setelah iterasi berikutnya dibuat.
            }
            if (failed) { result.ok = false; return result; }

            // perbaiki terminator latch clone c<N-1 -> headClone[c+1]
            for (std::uint32_t c = 0; c + 1 < N; ++c) {
                IrBlock& lb = fn.blocks[latchClone[c]];
                const InstrId tid = lb.insts.back();
                IrInstr t = fn.instrs[tid];  // snapshot
                t.operands = {IrOperand::block(headClone[c + 1])};
                if (!editor.replaceInstr(f, latchClone[c], lb.insts.size() - 1, t).ok) {
                    result.ok = false; return result;
                }
                lb.succs = {headClone[c + 1]};
            }

            // ---- rewiring keluar ----
            // pre -> headClone[0]
            {
                IrBlock& pb = fn.blocks[pre];
                const InstrId tid = pb.insts.back();
                IrInstr t = fn.instrs[tid];  // snapshot
                t.op = IrOpcode::Br;
                t.operands = {IrOperand::block(headClone[0])};
                if (!editor.replaceInstr(f, pre, pb.insts.size() - 1, t).ok) {
                    result.ok = false; return result;
                }
                pb.succs = {headClone[0]};
            }
            // E preds: ganti H -> latchClone[N-1]
            {
                IrBlock& eb = fn.blocks[E];
                for (BlockId& p : eb.preds) {
                    if (p == H) p = latchClone[N - 1];
                }
            }
            // remap untuk nilai "setelah loop" (setara awal iterasi N)
            auto remapAfter = [&](const IrOperand& op) -> IrOperand {
                if (op.kind != IrOperandKind::ValueV) return op;
                const ValueId vid = op.id;
                const auto itp = phiUpd.find(vid);
                if (itp != phiUpd.end()) {
                    const auto it = iterMap[N - 1].find(itp->second);
                    if (it != iterMap[N - 1].end()) return IrOperand::value(it->second);
                    return op;
                }
                const auto it = iterMap[N - 1].find(vid);
                if (it != iterMap[N - 1].end()) return IrOperand::value(it->second);
                return op;
            };
            // exit phi di E: pair [H, v] -> [latchClone[N-1], remapAfter(v)]
            {
                IrBlock& eb = fn.blocks[E];
                for (const InstrId iid : eb.insts) {
                    if (iid >= fn.instrs.size()) continue;
                    const IrInstr& ins = fn.instrs[iid];
                    if (ins.op != IrOpcode::Phi) continue;
                    if (ins.name == "exit_peel") continue;
                    for (std::size_t i = 0; i + 1 < ins.operands.size(); i += 2) {
                        if (ins.operands[i].kind == IrOperandKind::BlockV &&
                            ins.operands[i].id == static_cast<std::uint32_t>(H)) {
                            for (std::size_t pos = 0; pos < eb.insts.size(); ++pos) {
                                if (eb.insts[pos] == iid) {
                                    editor.replaceOperandAt(f, E, pos, i,
                                        IrOperand::block(latchClone[N - 1]));
                                    editor.replaceOperandAt(f, E, pos, i + 1,
                                        remapAfter(ins.operands[i + 1]));
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            // use phi/def-H/def-chain di luar loop (block asli non-loop)
            for (BlockId b = 0; b < nBlocks; ++b) {
                if (inLoop[b]) continue;
                IrBlock& blk = fn.blocks[b];
                for (std::size_t pos = 0; pos < blk.insts.size(); ++pos) {
                    const InstrId iid = blk.insts[pos];
                    if (iid >= fn.instrs.size()) continue;
                    if (fn.instrs[iid].op == IrOpcode::Phi) continue;
                    const std::vector<IrOperand> ops = fn.instrs[iid].operands;
                    for (std::size_t oi = 0; oi < ops.size(); ++oi) {
                        const IrOperand rn = remapAfter(ops[oi]);
                        if (rn.id != ops[oi].id || rn.kind != ops[oi].kind) {
                            if (!editor.replaceOperandAt(f, b, pos, oi, rn).ok) {
                                result.ok = false; return result;
                            }
                        }
                    }
                }
            }

            // sapu loop lama (tak terjangkau dari entry)
            std::uint32_t removed = 0, fcb = 0;
            if (!editor.removeUnreachableBlocks(f, removed, fcb).ok) {
                result.ok = false;
                return result;
            }
            result.changed = true;
            result.folded += N;
        }
    }
    return result;
}

} // namespace rhc
