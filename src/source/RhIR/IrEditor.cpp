#include "RhIR/IrEditor.h"

#include "RhIR/IrFunction.h"
#include "RhIR/IrModule.h"

namespace rhc {

namespace {
// Bump counter epoch module — sinyal mutasi untuk OptimizerManager.
void touchModuleEpoch(IrModule& m) { m.epoch += 1; }
} // namespace

bool irOpIsPureForDce(IrOpcode op) {
    switch (op) {
    case IrOpcode::ConstInt:
    case IrOpcode::ConstFloat:
    case IrOpcode::ConstBool:
    case IrOpcode::ConstString:
    case IrOpcode::Add:
    case IrOpcode::Sub:
    case IrOpcode::Mul:
    case IrOpcode::Div:
    case IrOpcode::Mod:
    case IrOpcode::Pow:
    case IrOpcode::DivFloor:
    case IrOpcode::BitAnd:
    case IrOpcode::BitOr:
    case IrOpcode::BitXor:
    case IrOpcode::Shl:
    case IrOpcode::Shr:
    case IrOpcode::Neg:
    case IrOpcode::BitNot:
    case IrOpcode::Not:
    case IrOpcode::And:
    case IrOpcode::Or:
    case IrOpcode::CmpEq:
    case IrOpcode::CmpNe:
    case IrOpcode::CmpLt:
    case IrOpcode::CmpLe:
    case IrOpcode::CmpGt:
    case IrOpcode::CmpGe:
    case IrOpcode::Cast:
    case IrOpcode::Construct:
    case IrOpcode::Extract:
    case IrOpcode::Insert:
        return true;
    default:
        return false;
    }
}

IrEditor::IrEditor(IrModule& module, IrEditorOptions options)
    : module_(&module), options_(options) {}

IrEditor::~IrEditor() = default;

IrInstr* IrEditor::locateInstr(FuncId func, BlockId block,
                               std::size_t posIndex,
                               IrEditorReport& report) const {
    if (module_ == nullptr) {
        report = {false, {IrEditorErrorCode::NullModule, "modul null"}};
        return nullptr;
    }
    if (func == InvalidFunc || func >= module_->functions.size()) {
        report = {false, {IrEditorErrorCode::InvalidFunction,
                          "FuncId di luar tabel fungsi"}};
        return nullptr;
    }
    IrFunction& fn = module_->functions[func];
    if (block == InvalidBlock || block >= fn.blocks.size()) {
        report = {false, {IrEditorErrorCode::InvalidBlock,
                          "BlockId di luar tabel block fungsi"}};
        return nullptr;
    }
    IrBlock& blk = fn.blocks[block];
    if (posIndex >= blk.insts.size()) {
        report = {false, {IrEditorErrorCode::InvalidInstrPosition,
                          "posIndex di luar urutan instruksi block"}};
        return nullptr;
    }
    const InstrId iid = blk.insts[posIndex];
    if (iid == InvalidInstr || iid >= fn.instrs.size()) {
        report = {false, {IrEditorErrorCode::InvalidInstrPosition,
                          "InstrId block merujuk di luar tabel instruksi"}};
        return nullptr;
    }
    return &fn.instrs[iid];
}

IrEditorReport IrEditor::afterMutation(const char* opName) {
    if (!options_.verifyEachMutation) {
        return {true, {IrEditorErrorCode::None, {}}};
    }
    IrVerifyReport verifyReport = IrVerifier{}.verify(*module_, options_.verifyLevel);
    if (verifyReport.ok()) {
        return {true, {IrEditorErrorCode::None, {}}};
    }
    std::string message = std::string(opName) +
        " membuat modul tak valid menurut verifier";
    if (!verifyReport.errors.empty()) {
        message += ": " + verifyReport.errors.front().message;
    }
    return {false, {IrEditorErrorCode::VerifyFailed, message}};
}

IrEditorReport IrEditor::replaceInstr(FuncId func, BlockId block,
                                      std::size_t posIndex,
                                      const IrInstr& instr) {
    IrEditorReport report;
    IrInstr* target = locateInstr(func, block, posIndex, report);
    if (target == nullptr) return report;
    if (instr.resultType != InvalidType &&
        (instr.resultType >= module_->types.size())) {
        return {false, {IrEditorErrorCode::InvalidTypeRef,
                        "resultType merujuk di luar tabel tipe"}};
    }
    if (target->resultType != instr.resultType) {
        return {false, {IrEditorErrorCode::ResultTypeMismatch,
                        "replaceInstr tidak boleh mengubah resultType; "
                        "gunakan setInstrResultType"}};
    }
    const std::uint32_t oldEpoch = target->epoch;
    *target = instr;
    target->epoch = oldEpoch + 1;
    touchModuleEpoch(*module_);
    return afterMutation("replaceInstr");
}

IrEditorReport IrEditor::replaceOperandAt(FuncId func, BlockId block,
                                          std::size_t posIndex,
                                          std::size_t operandIndex,
                                          IrOperand operand) {
    IrEditorReport report;
    IrInstr* target = locateInstr(func, block, posIndex, report);
    if (target == nullptr) return report;
    if (operandIndex >= target->operands.size()) {
        return {false, {IrEditorErrorCode::InvalidOperandIndex,
                        "operandIndex di luar operands instruksi"}};
    }
    target->operands[operandIndex] = operand;
    target->epoch += 1;
    touchModuleEpoch(*module_);
    return afterMutation("replaceOperandAt");
}

IrEditorReport IrEditor::setInstrResultType(FuncId func, BlockId block,
                                            std::size_t posIndex, TypeId type) {
    IrEditorReport report;
    IrInstr* target = locateInstr(func, block, posIndex, report);
    if (target == nullptr) return report;
    if (type != InvalidType && type >= module_->types.size()) {
        return {false, {IrEditorErrorCode::InvalidTypeRef,
                        "TypeId merujuk di luar tabel tipe"}};
    }
    // Sinkronkan tipe SSA value yang menghasilkan instruksi ini.
    const InstrId iid = module_->functions[func].blocks[block].insts[posIndex];
    IrFunction& fn = module_->functions[func];
    for (IrValue& v : fn.values) {
        if (v.kind == IrValue::Kind::Instr && v.payload == iid) {
            v.type = type;
        }
    }
    target->resultType = type;
    target->epoch += 1;
    touchModuleEpoch(*module_);
    return afterMutation("setInstrResultType");
}

IrEditorReport IrEditor::replaceConst(ConstId id,
                                      const IrConstant& constant) {
    if (module_ == nullptr) {
        return {false, {IrEditorErrorCode::NullModule, "modul null"}};
    }
    if (id == InvalidConst || id >= module_->constants.size()) {
        return {false, {IrEditorErrorCode::InvalidConst,
                        "ConstId di luar constant pool"}};
    }
    IrConstant& target = module_->constants[id];
    if (target.kind != constant.kind || target.type != constant.type) {
        return {false, {IrEditorErrorCode::ConstTypeMismatch,
                        "replaceConst menuntut kind dan type yang sama"}};
    }
    target = constant;
    touchModuleEpoch(*module_);
    return afterMutation("replaceConst");
}

IrEditorReport IrEditor::appendConst(const IrConstant& constant,
                                     ConstId& outId) {
    if (module_ == nullptr) {
        return {false, {IrEditorErrorCode::NullModule, "modul null"}};
    }
    if (constant.type != InvalidType &&
        constant.type >= module_->types.size()) {
        return {false, {IrEditorErrorCode::InvalidTypeRef,
                        "tipe konstanta merujuk di luar tabel tipe"}};
    }
    outId = static_cast<ConstId>(module_->constants.size());
    module_->constants.push_back(constant);
    touchModuleEpoch(*module_);
    return afterMutation("appendConst");
}

IrEditorReport IrEditor::compactFunctionTables(
    IrFunction& fn, const std::vector<bool>& dead) {
    // 1) Padatkan tabel instruksi; bangun peta InstrId lama -> baru.
    std::vector<InstrId> imap(fn.instrs.size(), InvalidInstr);
    std::vector<IrInstr> keptInstrs;
    keptInstrs.reserve(fn.instrs.size());
    for (std::uint32_t i = 0; i < fn.instrs.size(); ++i) {
        if (i < dead.size() && dead[i]) continue;
        imap[i] = static_cast<InstrId>(keptInstrs.size());
        keptInstrs.push_back(fn.instrs[i]);
    }
    fn.instrs = std::move(keptInstrs);
    for (IrBlock& blk : fn.blocks) {
        std::vector<InstrId> ordered;
        ordered.reserve(blk.insts.size());
        for (const InstrId iid : blk.insts) {
            if (iid < imap.size() && imap[iid] != InvalidInstr) {
                ordered.push_back(imap[iid]);
            }
        }
        blk.insts = std::move(ordered);
        blk.epoch += 1;
    }

    // 2) Padatkan value table; buang value dari instruksi yang dihapus.
    std::vector<ValueId> vmap(fn.values.size(), InvalidValue);
    std::vector<IrValue> keptValues;
    keptValues.reserve(fn.values.size());
    for (std::uint32_t i = 0; i < fn.values.size(); ++i) {
        IrValue v = fn.values[i];
        if (v.kind == IrValue::Kind::Instr) {
            if (v.payload >= imap.size() || imap[v.payload] == InvalidInstr) {
                continue;
            }
            v.payload = imap[v.payload];
        }
        vmap[i] = static_cast<ValueId>(keptValues.size());
        keptValues.push_back(std::move(v));
    }
    fn.values = std::move(keptValues);

    // 3) Remap SEMUA referensi ValueId function-lokal.
    for (IrInstr& instr : fn.instrs) {
        for (IrOperand& op : instr.operands) {
            if (op.kind == IrOperandKind::ValueV &&
                op.id < vmap.size() && vmap[op.id] != InvalidValue) {
                op.id = vmap[op.id];
            }
        }
        instr.epoch += 1;
    }
    for (ValueId& p : fn.params) {
        if (p < vmap.size() && vmap[p] != InvalidValue) p = vmap[p];
    }
    // locals: entry yang merujuk value yang DIBUANG berarti binding-nya
    // ikut mati bersama writer-nya — hapus entry; yang selamat di-remap.
    for (auto it = fn.locals.begin(); it != fn.locals.end();) {
        const ValueId p = it->second;
        if (p >= vmap.size() || vmap[p] == InvalidValue) {
            it = fn.locals.erase(it);
        } else {
            it->second = vmap[p];
            ++it;
        }
    }
    fn.epoch += 1;
    touchModuleEpoch(*module_);
    return {true, {IrEditorErrorCode::None, {}}};
}

IrEditorReport IrEditor::removeDeadInstrs(FuncId func,
                                          std::uint32_t& removedCount) {
    removedCount = 0;
    IrEditorReport report;
    if (module_ == nullptr) {
        return {false, {IrEditorErrorCode::NullModule, "modul null"}};
    }
    if (func == InvalidFunc || func >= module_->functions.size()) {
        return {false, {IrEditorErrorCode::InvalidFunction,
                        "FuncId di luar tabel fungsi"}};
    }
    IrFunction& fn = module_->functions[func];


    // 1) Hitung pemakaian tiap SSA value (operand ValueV, fungsi ini).
    std::vector<std::uint32_t> uses(fn.values.size(), 0);
    for (const IrBlock& blk : fn.blocks) {
        for (const InstrId iid : blk.insts) {
            if (iid == InvalidInstr || iid >= fn.instrs.size()) continue;
            for (const IrOperand& op : fn.instrs[iid].operands) {
                if (op.kind == IrOperandKind::ValueV &&
                    op.id < uses.size()) {
                    uses[op.id] += 1;
                }
            }
        }
    }

    // 2) Tandai instruksi mati: ter-urut, op murni, punya SSA value,
    //    dan value-nya tidak dipakai.
    std::vector<bool> dead(fn.instrs.size(), false);
    for (const IrBlock& blk : fn.blocks) {
        for (const InstrId iid : blk.insts) {
            if (iid == InvalidInstr || iid >= fn.instrs.size()) continue;
            const IrInstr& instr = fn.instrs[iid];
            if (!irOpIsPureForDce(instr.op)) continue;
            std::uint32_t vid = InvalidValue;
            for (std::uint32_t i = 0; i < fn.values.size(); ++i) {
                if (fn.values[i].kind == IrValue::Kind::Instr &&
                    fn.values[i].payload == iid) {
                    vid = i;
                    break;
                }
            }
            if (vid == InvalidValue || vid >= uses.size() || uses[vid] > 0) {
                continue;
            }
            dead[iid] = true;
            removedCount += 1;
        }
    }
    if (removedCount == 0) {
        return {true, {IrEditorErrorCode::None, {}}};
    }
    compactFunctionTables(fn, dead);
    touchModuleEpoch(*module_);
    return afterMutation("removeDeadInstrs");
}

IrEditorReport IrEditor::removeUnreachableBlocks(
    FuncId func, std::uint32_t& removedBlocks, std::uint32_t& foldedCondBr) {
    removedBlocks = 0;
    foldedCondBr = 0;
    IrEditorReport report;
    if (module_ == nullptr) {
        return {false, {IrEditorErrorCode::NullModule, "modul null"}};
    }
    if (func == InvalidFunc || func >= module_->functions.size()) {
        return {false, {IrEditorErrorCode::InvalidFunction,
                        "FuncId di luar tabel fungsi"}};
    }
    IrFunction& fn = module_->functions[func];
    if (fn.blocks.empty()) {
        return {true, {IrEditorErrorCode::None, {}}};
    }

    // Resolusi kondisi konstanta (ConstBool def atau ImmV) untuk
    // CondBr-fold; mengembalikan true + nilai bila konstan.
    const auto constBoolOf = [&fn](const IrOperand& op, bool& out) -> bool {
        if (op.kind == IrOperandKind::ImmV) {
            out = op.id != 0;
            return true;
        }
        if (op.kind != IrOperandKind::ValueV ||
            op.id >= fn.values.size()) {
            return false;
        }
        const IrValue& v = fn.values[op.id];
        if (v.kind != IrValue::Kind::Instr ||
            v.payload >= fn.instrs.size()) {
            return false;
        }
        const IrInstr& def = fn.instrs[v.payload];
        if (def.op != IrOpcode::ConstBool || def.operands.size() != 1 ||
            def.operands[0].kind != IrOperandKind::ImmV) {
            return false;
        }
        out = def.operands[0].id != 0;
        return true;
    };

    // 1) Iterasi internal sampai stabil: fold CondBr konstan (+ perbaikan
    //    preds & incoming phi target yang kehilangan edge), lalu hitung
    //    jangkauan dan buang block tak terjangkau.
    bool progress = true;
    while (progress) {
        progress = false;

        // 1a) CondBr dengan kondisi konstan -> Br ke target terambil.
        for (BlockId cur = 0; cur < fn.blocks.size(); ++cur) {
            IrBlock& blk = fn.blocks[cur];
            if (blk.insts.empty()) continue;
            const InstrId termId = blk.insts.back();
            if (termId >= fn.instrs.size()) continue;
            IrInstr& term = fn.instrs[termId];
            if (term.op != IrOpcode::CondBr ||
                term.operands.size() != 3 ||
                term.operands[1].kind != IrOperandKind::BlockV ||
                term.operands[2].kind != IrOperandKind::BlockV) {
                continue;
            }
            bool cond = false;
            if (!constBoolOf(term.operands[0], cond)) continue;
            const BlockId taken =
                cond ? term.operands[1].id : term.operands[2].id;
            const BlockId untaken =
                cond ? term.operands[2].id : term.operands[1].id;
            // Instruksi terminator menjadi Br target terambil.
            term.op = IrOpcode::Br;
            term.operands = {IrOperand::block(taken)};
            term.epoch += 1;
            foldedCondBr += 1;
            // Succs block ini hanya target terambil.
            blk.succs.clear();
            blk.succs.push_back(taken);
            blk.epoch += 1;
            // Target terambil: pastikan preds memuat cur (sudah ada edge).
            // Target terbuang: hapus edge cur -> perbaiki preds DAN
            // incoming phi (operand phi dipetakan per posisi preds).
            if (untaken < fn.blocks.size() && untaken != taken) {
                IrBlock& ub = fn.blocks[untaken];
                bool hadEdge = false;
                for (std::size_t i = 0; i < ub.preds.size(); ++i) {
                    if (ub.preds[i] == cur) {
                        ub.preds.erase(ub.preds.begin() + i);
                        hadEdge = true;
                        break;
                    }
                }
                if (hadEdge) {
                    // Incoming phi dikodekan sebagai PASANGAN
                    // [BlockV sumber, ValueV]: buang setiap pasangan yang
                    // BlockV-nya == cur.
                    for (const InstrId piid : ub.insts) {
                        if (piid >= fn.instrs.size()) continue;
                        IrInstr& pinstr = fn.instrs[piid];
                        if (pinstr.op != IrOpcode::Phi) continue;
                        std::vector<IrOperand> keptOps;
                        keptOps.reserve(pinstr.operands.size());
                        for (std::size_t i = 0; i + 1 < pinstr.operands.size();
                             i += 2) {
                            const bool drop =
                                pinstr.operands[i].kind ==
                                    IrOperandKind::BlockV &&
                                pinstr.operands[i].id == cur;
                            if (!drop) {
                                keptOps.push_back(pinstr.operands[i]);
                                keptOps.push_back(pinstr.operands[i + 1]);
                            }
                        }
                        pinstr.operands = std::move(keptOps);
                        pinstr.epoch += 1;
                    }
                    ub.epoch += 1;
                }
            }
            progress = true;
        }

        // 1b) Jangkauan dari block entry (blocks[0]) lewat succs.
        std::vector<bool> reachable(fn.blocks.size(), false);
        std::vector<BlockId> stack{0};
        reachable[0] = true;
        while (!stack.empty()) {
            const BlockId cur = stack.back();
            stack.pop_back();
            if (cur >= fn.blocks.size()) continue;
            for (const BlockId nxt : fn.blocks[cur].succs) {
                if (nxt < reachable.size() && !reachable[nxt]) {
                    reachable[nxt] = true;
                    stack.push_back(nxt);
                }
            }
        }
        std::uint32_t roundRemoved = 0;
        for (std::uint32_t i = 0; i < fn.blocks.size(); ++i) {
            if (!reachable[i]) roundRemoved += 1;
        }
        if (roundRemoved == 0) continue;  // stabil pada putaran ini
        progress = true;
        removedBlocks += roundRemoved;

        // 1c) Tandai instruksi mati: ter-urut di block tak terjangkau.
        std::vector<bool> dead(fn.instrs.size(), false);
        for (std::uint32_t bi = 0; bi < fn.blocks.size(); ++bi) {
            if (reachable[bi]) continue;
            for (const InstrId iid : fn.blocks[bi].insts) {
                if (iid < dead.size()) dead[iid] = true;
            }
        }

        // 1c-2) Trim incoming phi pada block SELAMAT sebelum orphan
        // rescue: operand phi dipetakan per posisi preds; indeks yang
        // preds-nya tak terjangkau dibuang (urutan predSurvived dipakai
        // ulang oleh pemadatan di 1e).
        for (std::uint32_t bi = 0; bi < fn.blocks.size(); ++bi) {
            if (!reachable[bi]) continue;
            IrBlock& blk = fn.blocks[bi];
            std::vector<std::uint8_t> survived(blk.preds.size(), 0);
            std::size_t nSurvived = 0;
            for (std::size_t i = 0; i < blk.preds.size(); ++i) {
                if (blk.preds[i] < reachable.size() &&
                    reachable[blk.preds[i]]) {
                    survived[i] = 1;
                    ++nSurvived;
                }
            }
            if (nSurvived == blk.preds.size()) continue;
            for (const InstrId piid : blk.insts) {
                if (piid >= fn.instrs.size()) continue;
                IrInstr& pinstr = fn.instrs[piid];
                if (pinstr.op != IrOpcode::Phi ||
                    pinstr.operands.size() % 2 != 0) {
                    continue;
                }
                std::vector<IrOperand> keptOps;
                keptOps.reserve(pinstr.operands.size());
                for (std::size_t i = 0; i + 1 < pinstr.operands.size();
                     i += 2) {
                    const IrOperand& src = pinstr.operands[i];
                    const bool keep =
                        src.kind == IrOperandKind::BlockV &&
                        src.id < reachable.size() && reachable[src.id];
                    if (keep) {
                        keptOps.push_back(pinstr.operands[i]);
                        keptOps.push_back(pinstr.operands[i + 1]);
                    }
                }
                pinstr.operands = std::move(keptOps);
                pinstr.epoch += 1;
            }
        }

        // 1d) Catatan invariant: IR mewajibkan SEMUA instruksi ter-urut
        // di sebuah block (structural verify). Setelah trim pasangan phi
        // yang benar, tidak ada instruksi hidup yang boleh merujuk value
        // dari block tak terjangkau (SSA: use terdominasi def; satu-satunya
        // pengecualian, phi, sudah ditangani di 1a/1c-2).

        // 1e) Padatkan tabel block; remap BlockId + operand BlockV.
        std::vector<BlockId> bmap(fn.blocks.size(), InvalidBlock);
        std::vector<IrBlock> keptBlocks;
        keptBlocks.reserve(fn.blocks.size() - roundRemoved);
        for (std::uint32_t i = 0; i < fn.blocks.size(); ++i) {
            if (!reachable[i]) continue;
            bmap[i] = static_cast<BlockId>(keptBlocks.size());
            keptBlocks.push_back(std::move(fn.blocks[i]));
        }
        fn.blocks = std::move(keptBlocks);
        for (IrBlock& blk : fn.blocks) {
            std::vector<BlockId> kept;
            for (const BlockId p : blk.preds) {
                if (p < bmap.size() && bmap[p] != InvalidBlock) {
                    kept.push_back(bmap[p]);
                }
            }
            blk.preds = std::move(kept);
            kept.clear();
            for (const BlockId sx : blk.succs) {
                if (sx < bmap.size() && bmap[sx] != InvalidBlock) {
                    kept.push_back(bmap[sx]);
                }
            }
            blk.succs = std::move(kept);
            blk.epoch += 1;
        }
        for (IrInstr& instr : fn.instrs) {
            for (IrOperand& op : instr.operands) {
                if (op.kind == IrOperandKind::BlockV &&
                    op.id < bmap.size() && bmap[op.id] != InvalidBlock) {
                    op.id = bmap[op.id];
                }
            }
        }

        compactFunctionTables(fn, dead);
        // Lanjut putaran berikutnya: fold CondBr baru bisa membuka
        // ketak-terjangkauan baru.
    }

    // Epoch bump HANYA bila benar-benar ada mutasi: fold CondBr atau
    // penghapusan block (change-detection net OptimizerManager bergantung
    // pada invariant: epochDelta > 0 <-> ada mutasi nyata).
    if (foldedCondBr > 0 || removedBlocks > 0) {
        touchModuleEpoch(*module_);
    }
    return afterMutation("removeUnreachableBlocks");
}

IrEditorReport IrEditor::insertInstrBefore(FuncId func, BlockId block,
                                           std::size_t posIndex,
                                           const IrInstr& instr,
                                           ValueId& outValue) {
    outValue = InvalidValue;
    IrEditorReport report;
    if (module_ == nullptr) {
        return {false, {IrEditorErrorCode::NullModule, "modul null"}};
    }
    if (func == InvalidFunc || func >= module_->functions.size()) {
        return {false, {IrEditorErrorCode::InvalidFunction,
                        "FuncId di luar tabel fungsi"}};
    }
    IrFunction& fn = module_->functions[func];
    if (block == InvalidBlock || block >= fn.blocks.size()) {
        return {false, {IrEditorErrorCode::InvalidBlock,
                        "BlockId di luar tabel block fungsi"}};
    }
    IrBlock& blk = fn.blocks[block];
    if (posIndex > blk.insts.size()) {
        return {false, {IrEditorErrorCode::InvalidInstrPosition,
                        "posIndex di luar urutan instruksi block"}};
    }
    if (instr.resultType != InvalidType &&
        instr.resultType >= module_->types.size()) {
        return {false, {IrEditorErrorCode::InvalidTypeRef,
                        "resultType merujuk di luar tabel tipe"}};
    }
    // Append tabel instruksi: InstrId baru di ujung.
    const InstrId newId = static_cast<InstrId>(fn.instrs.size());
    fn.instrs.push_back(instr);
    blk.insts.insert(blk.insts.begin() + posIndex, newId);
    blk.epoch += 1;
    // Daftarkan SSA value bila instruksi menghasilkan nilai.
    if (instr.resultType != InvalidType) {
        IrValue v;
        v.kind = IrValue::Kind::Instr;
        v.payload = newId;
        v.type = instr.resultType;
        outValue = static_cast<ValueId>(fn.values.size());
        fn.values.push_back(std::move(v));
    }
    fn.epoch += 1;
    return afterMutation("insertInstrBefore");
}

IrEditorReport IrEditor::appendBlock(FuncId func, BlockId& outBlock) {
    outBlock = InvalidBlock;
    if (module_ == nullptr) {
        return {false, {IrEditorErrorCode::NullModule, "modul null"}};
    }
    if (func == InvalidFunc || func >= module_->functions.size()) {
        return {false, {IrEditorErrorCode::InvalidFunction,
                        "FuncId di luar tabel fungsi"}};
    }
    IrFunction& fn = module_->functions[func];
    IrBlock blk;
    outBlock = static_cast<BlockId>(fn.blocks.size());
    fn.blocks.push_back(std::move(blk));
    fn.epoch += 1;
    touchModuleEpoch(*module_);
    return afterMutation("appendBlock");
}

IrEditorReport IrEditor::appendOperandAt(FuncId func, BlockId block,
                                         std::size_t posIndex,
                                         IrOperand operand) {
    IrEditorReport report;
    IrInstr* target = locateInstr(func, block, posIndex, report);
    if (target == nullptr) return report;
    target->operands.push_back(operand);
    target->epoch += 1;
    touchModuleEpoch(*module_);
    return afterMutation("appendOperandAt");
}

IrEditorReport IrEditor::validate(IrVerifyLevel level,
                                  const IrTargetProvider* targetProvider) const {
    if (module_ == nullptr) {
        return {false, {IrEditorErrorCode::NullModule, "modul null"}};
    }
    IrVerifyReport verifyReport = IrVerifier{}.verify(*module_, level, targetProvider);
    IrEditorReport report;
    report.ok = verifyReport.ok();
    if (!report.ok) {
        std::string message = "validate: modul tidak lolos verifier";
        if (!verifyReport.errors.empty()) {
            message += ": " + verifyReport.errors.front().message;
        }
        report.error = {IrEditorErrorCode::VerifyFailed, message};
    }
    return report;
}

} // namespace rhc
