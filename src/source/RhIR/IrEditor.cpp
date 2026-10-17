#include "RhIR/IrEditor.h"

#include "RhIR/IrFunction.h"
#include "RhIR/IrModule.h"

namespace rhc {

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
    return afterMutation("removeDeadInstrs");
}

IrEditorReport IrEditor::removeUnreachableBlocks(
    FuncId func, std::uint32_t& removedBlocks) {
    removedBlocks = 0;
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

    // 1) Jangkauan dari block entry (blocks[0]) lewat rantai succs.
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
    for (std::uint32_t i = 0; i < fn.blocks.size(); ++i) {
        if (!reachable[i]) removedBlocks += 1;
    }
    if (removedBlocks == 0) {
        return {true, {IrEditorErrorCode::None, {}}};
    }

    // 2) Tandai instruksi mati: ter-urut di block yang tak terjangkau.
    std::vector<bool> dead(fn.instrs.size(), false);
    for (std::uint32_t bi = 0; bi < fn.blocks.size(); ++bi) {
        if (reachable[bi]) continue;
        for (const InstrId iid : fn.blocks[bi].insts) {
            if (iid < dead.size()) dead[iid] = true;
        }
    }

    // 3) Padatkan tabel block; bangun peta BlockId lama -> baru.
    std::vector<BlockId> bmap(fn.blocks.size(), InvalidBlock);
    std::vector<IrBlock> keptBlocks;
    keptBlocks.reserve(fn.blocks.size() - removedBlocks);
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
        for (const BlockId s : blk.succs) {
            if (s < bmap.size() && bmap[s] != InvalidBlock) {
                kept.push_back(bmap[s]);
            }
        }
        blk.succs = std::move(kept);
        blk.epoch += 1;
    }

    // 4) Remap operand BlockV (target cabang / incoming phi) pada
    //    instruksi yang selamat — instruksi di block mati ikut terbuang
    //    oleh compactFunctionTables.
    for (IrInstr& instr : fn.instrs) {
        for (IrOperand& op : instr.operands) {
            if (op.kind == IrOperandKind::BlockV &&
                op.id < bmap.size() && bmap[op.id] != InvalidBlock) {
                op.id = bmap[op.id];
            }
        }
    }

    compactFunctionTables(fn, dead);
    return afterMutation("removeUnreachableBlocks");
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
