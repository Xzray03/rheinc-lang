#include "RhIR/IrBuilder.h"

#include "RhCore/DiagnosticEngine.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace rhc {

namespace {

const std::vector<IrBuilder::Use>& emptyUses() {
    static const std::vector<IrBuilder::Use> kEmpty;
    return kEmpty;
}

IrFunction& dummyFunction() {
    static IrFunction fn;
    return fn;
}

IrBlock& dummyBlock() {
    static IrBlock blk;
    return blk;
}

// Perbandingan struktural dua tipe untuk dedup interning primitif.
bool sameTypeShape(const IrType& a, const IrType& b) {
    if (a.kind != b.kind || a.name != b.name ||
        a.sizeBytes != b.sizeBytes || a.alignBytes != b.alignBytes ||
        a.isSigned != b.isSigned || a.elementType != b.elementType ||
        a.arraySize != b.arraySize || a.returnType != b.returnType ||
        a.paramTypes != b.paramTypes) {
        return false;
    }
    if (a.fields.size() != b.fields.size() ||
        a.variants.size() != b.variants.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.fields.size(); ++i) {
        const IrField& x = a.fields[i];
        const IrField& y = b.fields[i];
        if (x.name != y.name || x.type != y.type ||
            x.offsetBytes != y.offsetBytes ||
            x.bitOffset != y.bitOffset || x.bitWidth != y.bitWidth) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.variants.size(); ++i) {
        const IrVariant& x = a.variants[i];
        const IrVariant& y = b.variants[i];
        if (x.name != y.name || x.payloadType != y.payloadType ||
            x.tagValue != y.tagValue) {
            return false;
        }
    }
    return true;
}

bool sameConstant(const IrConstant& a, const IrConstant& b) {
    return a.kind == b.kind && a.type == b.type &&
           a.bitPattern == b.bitPattern && a.length == b.length &&
           a.bytes == b.bytes;
}

} // namespace

struct IrBuilder::Impl {
    Impl(IrModule& moduleRef, DiagnosticEngine& diagnosticsRef,
         const IrBuilderOptions& optionsRef)
        : module(moduleRef), diagnostics(diagnosticsRef), options(optionsRef) {}

    IrModule& module;
    DiagnosticEngine& diagnostics;
    // BY VALUE: struct kecil (size_t + bool) — referensi ke temporary
    // di call-site tidak boleh menjadi dangling (robustness API).
    IrBuilderOptions options;
    bool broken = false;

    FuncId insertFunc = InvalidFunc;
    BlockId insertBlock = InvalidBlock;

    // Use-list per fungsi (struktur bantu no. 2). Indeks luar = ValueId,
    // selalu dijaga sejajar dengan IrFunction::values.
    std::unordered_map<FuncId, std::vector<std::vector<Use>>> useLists;

    void fatal(const std::string& message) {
        diagnostics.fatal(FileID(FileID::invalidValue), 0, 0, message);
        broken = true;
    }

    bool validFunc(FuncId id) const {
        return id < module.functions.size();
    }

    bool validBlock(const IrFunction& fn, BlockId id) const {
        return id < fn.blocks.size();
    }

    void ensureUseListSize(FuncId func, const IrFunction& fn) {
        std::vector<std::vector<Use>>& lists = useLists[func];
        if (lists.size() < fn.values.size()) {
            lists.resize(fn.values.size());
        }
    }

    void addEdgeUnique(std::vector<BlockId>& edges, BlockId target) {
        if (std::find(edges.begin(), edges.end(), target) == edges.end()) {
            edges.push_back(target);
        }
    }

    void removeEdge(std::vector<BlockId>& edges, BlockId target) {
        edges.erase(std::remove(edges.begin(), edges.end(), target),
                    edges.end());
    }

    void registerUses(FuncId func, InstrId instr, const IrInstr& inst) {
        std::vector<std::vector<Use>>& lists = useLists[func];
        for (std::uint32_t slot = 0; slot < inst.operands.size(); ++slot) {
            if (inst.operands[slot].kind != IrOperandKind::ValueV) continue;
            const ValueId v = inst.operands[slot].id;
            if (v >= lists.size()) continue;  // dilaporkan verify()
            lists[v].push_back({instr, slot});
        }
    }

    void unregisterUses(FuncId func, InstrId instr, const IrInstr& inst) {
        std::vector<std::vector<Use>>& lists = useLists[func];
        for (std::uint32_t slot = 0; slot < inst.operands.size(); ++slot) {
            if (inst.operands[slot].kind != IrOperandKind::ValueV) continue;
            const ValueId v = inst.operands[slot].id;
            if (v >= lists.size()) continue;
            std::vector<Use>& vec = lists[v];
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [instr](const Use& u) {
                                         return u.instr == instr;
                                     }),
                      vec.end());
        }
    }

    // Tambah/hapus CFG edges dari semua operand BlockV sebuah terminator.
    void applyTerminatorEdges(FuncId func, BlockId from, const IrInstr& inst,
                              bool add) {
        IrFunction& fn = module.functions[func];
        for (const IrOperand& op : inst.operands) {
            if (op.kind != IrOperandKind::BlockV) continue;
            const BlockId to = op.id;
            if (!validBlock(fn, to)) continue;  // dilaporkan verify()
            if (add) {
                addEdgeUnique(fn.blocks[from].succs, to);
                addEdgeUnique(fn.blocks[to].preds, from);
            } else {
                removeEdge(fn.blocks[from].succs, to);
                removeEdge(fn.blocks[to].preds, from);
            }
        }
    }

    // Cari block yang memuat instruksi (InstrId unik per fungsi).
    BlockId findInstrBlock(const IrFunction& fn, InstrId instr) const {
        for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
            const IrBlock& blk = fn.blocks[b];
            if (std::find(blk.insts.begin(), blk.insts.end(), instr) !=
                blk.insts.end()) {
                return static_cast<BlockId>(b);
            }
        }
        return InvalidBlock;
    }

    void postMutationCheck(FuncId func) {
        if (!options.verifyAfterEachMutation) return;
        std::string error;
        if (!verifyFunc(func, &error, /*requireComplete=*/false)) {
            std::ostringstream oss;
            oss << "IrBuilder: invariant rusak setelah mutasi di fungsi '"
                << module.functions[func].name << "': " << error;
            fatal(oss.str());
        }
    }

    // requireComplete=false memeriksa invariant struktural yang wajib tahan
    // setiap mutasi; kondisi transient konstruksi (block belum ada isinya,
    // phi belum lengkap incoming) hanya diperiksa bila requireComplete.
    bool verifyFunc(FuncId func, std::string* errorOut,
                    bool requireComplete) const;
};

IrBuilder::IrBuilder(IrModule& module, DiagnosticEngine& diagnostics,
                     const IrBuilderOptions& options)
    : impl_(new Impl(module, diagnostics, options)) {}

IrBuilder::~IrBuilder() { delete impl_; }

const IrBuilderOptions& IrBuilder::options() const { return impl_->options; }

bool IrBuilder::broken() const { return impl_->broken; }

TypeId IrBuilder::internType(const IrType& type) {
    std::vector<IrType>& types = impl_->module.types;
    // Named type (Struct/Sum, dapat recursive) SELALU entri baru.
    const bool named = type.kind == IrTypeKind::Struct ||
                       type.kind == IrTypeKind::Sum;
    if (!named) {
        for (std::size_t i = 0; i < types.size(); ++i) {
            if (sameTypeShape(types[i], type)) {
                return static_cast<TypeId>(i);
            }
        }
    }
    types.push_back(type);
    return static_cast<TypeId>(types.size() - 1);
}

ConstId IrBuilder::internConstant(const IrConstant& constant) {
    std::vector<IrConstant>& constants = impl_->module.constants;
    for (std::size_t i = 0; i < constants.size(); ++i) {
        if (sameConstant(constants[i], constant)) {
            return static_cast<ConstId>(i);
        }
    }
    constants.push_back(constant);
    return static_cast<ConstId>(constants.size() - 1);
}

FuncId IrBuilder::createFunction(std::string name, TypeId fnType,
                                   SourceLocation loc, IrFunctionKind kind) {
    if (impl_->broken) return InvalidFunc;
    IrFunction fn;
    fn.name = std::move(name);
    fn.fnType = fnType;
    fn.kind = kind;
    fn.loc = loc;
    impl_->module.functions.push_back(std::move(fn));
    return static_cast<FuncId>(impl_->module.functions.size() - 1);
}

BlockId IrBuilder::createBlock(FuncId func, std::string label,
                               SourceLocation loc) {
    if (impl_->broken) return InvalidBlock;
    if (!impl_->validFunc(func)) {
        impl_->fatal("IrBuilder::createBlock: FuncId di luar tabel fungsi");
        return InvalidBlock;
    }
    IrFunction& fn = impl_->module.functions[func];
    IrBlock blk;
    blk.label = std::move(label);
    blk.loc = loc;
    fn.blocks.push_back(std::move(blk));
    return static_cast<BlockId>(fn.blocks.size() - 1);
}

void IrBuilder::setInsertPoint(FuncId func, BlockId block) {
    if (impl_->broken) return;
    if (!impl_->validFunc(func) ||
        !impl_->validBlock(impl_->module.functions[func], block)) {
        impl_->fatal("IrBuilder::setInsertPoint: ID fungsi/block tidak valid");
        return;
    }
    impl_->insertFunc = func;
    impl_->insertBlock = block;
}

IrFunction& IrBuilder::function(FuncId func) {
    if (!impl_->validFunc(func)) {
        impl_->fatal("IrBuilder::function: FuncId di luar tabel fungsi");
        return dummyFunction();
    }
    return impl_->module.functions[func];
}

const IrFunction& IrBuilder::function(FuncId func) const {
    if (!impl_->validFunc(func)) {
        impl_->fatal("IrBuilder::function: FuncId di luar tabel fungsi");
        return dummyFunction();
    }
    return impl_->module.functions[func];
}

IrBlock& IrBuilder::block(FuncId func, BlockId blockId) {
    if (!impl_->validFunc(func) ||
        !impl_->validBlock(impl_->module.functions[func], blockId)) {
        impl_->fatal("IrBuilder::block: ID block tidak valid");
        return dummyBlock();
    }
    return impl_->module.functions[func].blocks[blockId];
}

const IrBlock& IrBuilder::block(FuncId func, BlockId blockId) const {
    if (!impl_->validFunc(func) ||
        !impl_->validBlock(impl_->module.functions[func], blockId)) {
        impl_->fatal("IrBuilder::block: ID block tidak valid");
        return dummyBlock();
    }
    return impl_->module.functions[func].blocks[blockId];
}

ValueId IrBuilder::addParam(FuncId func, TypeId type, std::string name) {
    if (impl_->broken) return InvalidValue;
    if (!impl_->validFunc(func)) {
        impl_->fatal("IrBuilder::addParam: FuncId di luar tabel fungsi");
        return InvalidValue;
    }
    IrFunction& fn = impl_->module.functions[func];
    IrValue v;
    v.kind = IrValue::Kind::Param;
    v.payload = static_cast<std::uint32_t>(fn.params.size());
    v.type = type;
    v.name = std::move(name);
    fn.values.push_back(std::move(v));
    fn.params.push_back(static_cast<ValueId>(fn.values.size() - 1));
    impl_->ensureUseListSize(func, fn);
    impl_->postMutationCheck(func);
    return fn.values.size() - 1;
}

ValueId IrBuilder::emit(IrOpcode op, TypeId resultType,
                        std::vector<IrOperand> operands, SourceLocation loc,
                        std::string name) {
    if (impl_->broken) return InvalidValue;
    if (isTerminator(op)) {
        impl_->fatal(
            "IrBuilder::emit: instruksi terminator harus via setTerminator()");
        return InvalidValue;
    }
    if (impl_->insertFunc == InvalidFunc || impl_->insertBlock == InvalidBlock) {
        impl_->fatal("IrBuilder::emit: insert point belum di-set");
        return InvalidValue;
    }
    IrFunction& fn = impl_->module.functions[impl_->insertFunc];
    IrBlock& blk = fn.blocks[impl_->insertBlock];
    if (!blk.insts.empty() &&
        isTerminator(fn.instrs[blk.insts.back()].op)) {
        impl_->fatal(
            "IrBuilder::emit: block sudah ditutup terminator; "
            "buat block baru atau pakai replaceTerminator");
        return InvalidValue;
    }

    IrInstr inst;
    inst.op = op;
    inst.resultType = resultType;
    inst.loc = loc;
    inst.operands = std::move(operands);
    inst.name = std::move(name);
    inst.epoch = ++fn.epoch;

    const InstrId id = static_cast<InstrId>(fn.instrs.size());
    fn.instrs.push_back(std::move(inst));
    blk.insts.push_back(id);
    impl_->registerUses(impl_->insertFunc, id, fn.instrs[id]);

    ValueId value = InvalidValue;
    if (resultType != InvalidType) {
        IrValue v;
        v.kind = IrValue::Kind::Instr;
        v.payload = id;
        v.type = resultType;
        v.name = fn.instrs[id].name;
        fn.values.push_back(std::move(v));
        value = static_cast<ValueId>(fn.values.size() - 1);
        impl_->ensureUseListSize(impl_->insertFunc, fn);
    }

    impl_->postMutationCheck(impl_->insertFunc);
    return value;
}

void IrBuilder::setTerminator(FuncId func, BlockId blockId, IrOpcode op,
                              std::vector<IrOperand> operands,
                              SourceLocation loc, bool replaceTerminator) {
    if (impl_->broken) return;
    if (!isTerminator(op)) {
        impl_->fatal("IrBuilder::setTerminator: opcode bukan terminator");
        return;
    }
    if (!impl_->validFunc(func) ||
        !impl_->validBlock(impl_->module.functions[func], blockId)) {
        impl_->fatal("IrBuilder::setTerminator: ID fungsi/block tidak valid");
        return;
    }
    IrFunction& fn = impl_->module.functions[func];
    IrBlock& blk = fn.blocks[blockId];

    if (!blk.insts.empty() &&
        isTerminator(fn.instrs[blk.insts.back()].op)) {
        if (!replaceTerminator) {
            std::ostringstream oss;
            oss << "IrBuilder::setTerminator: block '" << blk.label
                << "' sudah punya terminator (invariant: tepat satu)";
            impl_->fatal(oss.str());
            return;
        }
        // Ganti terminator lama: cabut edges & uses-nya, lalu buang.
        const InstrId oldId = blk.insts.back();
        IrInstr& old = fn.instrs[oldId];
        impl_->applyTerminatorEdges(func, blockId, old, /*add=*/false);
        impl_->unregisterUses(func, oldId, old);
        old.op = IrOpcode::Dead;
        blk.insts.pop_back();
    }

    IrInstr inst;
    inst.op = op;
    inst.resultType = InvalidType;  // terminator tidak menghasilkan value
    inst.loc = loc;
    inst.operands = std::move(operands);
    inst.epoch = ++fn.epoch;

    const InstrId id = static_cast<InstrId>(fn.instrs.size());
    fn.instrs.push_back(std::move(inst));
    blk.insts.push_back(id);
    impl_->registerUses(func, id, fn.instrs[id]);
    impl_->applyTerminatorEdges(func, blockId, fn.instrs[id], /*add=*/true);

    impl_->postMutationCheck(func);
}

ValueId IrBuilder::createPhi(TypeId type, FuncId func, BlockId joinBlock,
                             SourceLocation loc, std::string name) {
    if (impl_->broken) return InvalidValue;
    if (!impl_->validFunc(func) ||
        !impl_->validBlock(impl_->module.functions[func], joinBlock)) {
        impl_->fatal("IrBuilder::createPhi: ID fungsi/block tidak valid");
        return InvalidValue;
    }
    IrFunction& fn = impl_->module.functions[func];
    IrBlock& blk = fn.blocks[joinBlock];
    // Phi loop-carried disisipkan SETELAH terminator header dipasang
    // (nilai incoming dari body baru diketahui setelah body di-lowering).
    // Invariant yang dijaga: phi tetap berada SEBELUM terminator.
    bool insertBeforeTerminator = false;
    if (!blk.insts.empty() &&
        isTerminator(fn.instrs[blk.insts.back()].op)) {
        insertBeforeTerminator = true;
    }

    IrInstr inst;
    inst.op = IrOpcode::Phi;
    inst.resultType = type;
    inst.loc = loc;
    inst.name = std::move(name);
    inst.epoch = ++fn.epoch;

    const InstrId id = static_cast<InstrId>(fn.instrs.size());
    fn.instrs.push_back(std::move(inst));
    if (insertBeforeTerminator) {
        blk.insts.insert(blk.insts.end() - 1, id);
    } else {
        blk.insts.push_back(id);
    }

    IrValue v;
    v.kind = IrValue::Kind::Instr;
    v.payload = id;
    v.type = type;
    v.name = fn.instrs[id].name;
    fn.values.push_back(std::move(v));
    impl_->ensureUseListSize(func, fn);

    impl_->postMutationCheck(func);
    return static_cast<ValueId>(fn.values.size() - 1);
}

void IrBuilder::addPhiIncoming(FuncId func, ValueId phi, BlockId pred,
                               ValueId value) {
    if (impl_->broken) return;
    if (!impl_->validFunc(func)) {
        impl_->fatal("IrBuilder::addPhiIncoming: FuncId tidak valid");
        return;
    }
    IrFunction& fn = impl_->module.functions[func];
    if (phi >= fn.values.size() ||
        fn.values[phi].kind != IrValue::Kind::Instr) {
        impl_->fatal("IrBuilder::addPhiIncoming: phi bukan value instruksi");
        return;
    }
    const InstrId id = fn.values[phi].payload;
    if (fn.instrs[id].op != IrOpcode::Phi) {
        impl_->fatal("IrBuilder::addPhiIncoming: instruksi bukan Phi");
        return;
    }

    fn.instrs[id].operands.push_back(IrOperand::block(pred));
    fn.instrs[id].operands.push_back(IrOperand::value(value));
    std::vector<std::vector<Use>>& lists = impl_->useLists[func];
    if (value < lists.size()) {
        lists[value].push_back(
            {id, static_cast<std::uint32_t>(fn.instrs[id].operands.size() - 1)});
    }
    fn.instrs[id].epoch = ++fn.epoch;

    impl_->postMutationCheck(func);
}

void IrBuilder::markDead(FuncId func, InstrId instr) {
    if (impl_->broken) return;
    if (!impl_->validFunc(func)) {
        impl_->fatal("IrBuilder::markDead: FuncId tidak valid");
        return;
    }
    IrFunction& fn = impl_->module.functions[func];
    if (instr >= fn.instrs.size()) {
        impl_->fatal("IrBuilder::markDead: InstrId di luar tabel");
        return;
    }
    IrInstr& inst = fn.instrs[instr];
    if (inst.op == IrOpcode::Dead) return;  // idempotent
    if (isTerminator(inst.op)) {
        impl_->fatal(
            "IrBuilder::markDead: terminator tidak boleh di-tombstone "
            "(pakai setTerminator replace)");
        return;
    }
    impl_->unregisterUses(func, instr, inst);
    inst.operands.clear();
    inst.op = IrOpcode::Dead;
    inst.epoch = ++fn.epoch;

    impl_->postMutationCheck(func);
}

void IrBuilder::compact(FuncId func) {
    if (impl_->broken) return;
    if (!impl_->validFunc(func)) {
        impl_->fatal("IrBuilder::compact: FuncId tidak valid");
        return;
    }
    IrFunction& fn = impl_->module.functions[func];

    // Fase 0 — keamanan: value yang penghasilnya Dead hanya boleh dibuang
    // bila use-list-nya kosong; bila masih dipakai = invariant internal.
    {
        const std::vector<std::vector<Use>>& lists =
            impl_->useLists[func];
        for (std::size_t v = 0; v < fn.values.size(); ++v) {
            const IrValue& value = fn.values[v];
            if (value.kind != IrValue::Kind::Instr) continue;
            if (value.payload >= fn.instrs.size()) continue;
            if (fn.instrs[value.payload].op != IrOpcode::Dead) continue;
            if (v < lists.size() && !lists[v].empty()) {
                std::ostringstream oss;
                oss << "IrBuilder::compact: value " << v << " ('"
                    << value.name << "') penghasilnya Dead tapi masih "
                    << "punya " << lists[v].size()
                    << " pemakaian (bug pass: ganti pemakaian dulu)";
                impl_->fatal(oss.str());
                return;
            }
        }
    }

    // Fase 1 — InstrId remap: instruksi Dead dibuang.
    const std::size_t oldInstrCount = fn.instrs.size();
    std::vector<InstrId> instrMap(oldInstrCount, InvalidInstr);
    std::vector<IrInstr> newInstrs;
    newInstrs.reserve(oldInstrCount);
    for (std::size_t i = 0; i < oldInstrCount; ++i) {
        if (fn.instrs[i].op == IrOpcode::Dead) continue;
        instrMap[i] = static_cast<InstrId>(newInstrs.size());
        newInstrs.push_back(std::move(fn.instrs[i]));
    }

    // Fase 2 — ValueId remap: value Param dipertahankan; value Instr
    // dipertahankan bila penghasilnya ikut dipertahankan.
    const std::size_t oldValueCount = fn.values.size();
    std::vector<ValueId> valueMap(oldValueCount, InvalidValue);
    std::vector<IrValue> newValues;
    newValues.reserve(oldValueCount);
    for (std::size_t v = 0; v < oldValueCount; ++v) {
        const IrValue& value = fn.values[v];
        bool keep = value.kind == IrValue::Kind::Param;
        if (value.kind == IrValue::Kind::Instr) {
            keep = value.payload < instrMap.size() &&
                   instrMap[value.payload] != InvalidInstr;
        }
        if (!keep) continue;
        if (value.kind == IrValue::Kind::Instr) {
            IrValue moved = value;
            moved.payload = instrMap[value.payload];
            valueMap[v] = static_cast<ValueId>(newValues.size());
            newValues.push_back(std::move(moved));
        } else {
            valueMap[v] = static_cast<ValueId>(newValues.size());
            newValues.push_back(value);
        }
    }

    // Fase 3 — remap operand instruksi (ValueV saja; BlockV/ConstId/FuncId/
    // TypeId tidak berubah karena tidak ada block/fungsi yang dihapus).
    for (IrInstr& inst : newInstrs) {
        for (IrOperand& op : inst.operands) {
            if (op.kind == IrOperandKind::ValueV &&
                op.id < valueMap.size()) {
                op.id = valueMap[op.id];
            }
        }
    }

    // Fase 4 — remap referensi: block insts, symbol table lokal, params.
    // Instruksi tombstone masih tercatat di blk.insts (markDead sengaja
    // tidak mengubah urutan block) — entri yang di-map ke InvalidInstr
    // (instruksi Dead) harus DIBUANG, bukan disalin.
    for (IrBlock& blk : fn.blocks) {
        std::vector<InstrId> kept;
        kept.reserve(blk.insts.size());
        for (InstrId id : blk.insts) {
            if (id < instrMap.size() && instrMap[id] != InvalidInstr) {
                kept.push_back(instrMap[id]);
            }
        }
        blk.insts = std::move(kept);
    }
    for (auto& [name, valueId] : fn.locals) {
        valueId = valueId < valueMap.size() ? valueMap[valueId]
                                            : InvalidValue;
    }
    for (ValueId& param : fn.params) {
        param = param < valueMap.size() ? valueMap[param] : InvalidValue;
    }

    // Fase 5 — bangun ulang use-list dari instruksi hasil remap.
    std::vector<std::vector<Use>> newLists(newValues.size());
    for (std::size_t i = 0; i < newInstrs.size(); ++i) {
        const IrInstr& inst = newInstrs[i];
        for (std::uint32_t slot = 0; slot < inst.operands.size(); ++slot) {
            if (inst.operands[slot].kind != IrOperandKind::ValueV) continue;
            const ValueId v = inst.operands[slot].id;
            if (v < newLists.size()) {
                newLists[v].push_back(
                    {static_cast<InstrId>(i), slot});
            }
        }
    }

    fn.instrs = std::move(newInstrs);
    fn.values = std::move(newValues);
    impl_->useLists[func] = std::move(newLists);
    ++fn.epoch;  // seluruh ID lama dianggap tidak valid
}

const std::vector<IrBuilder::Use>& IrBuilder::usesOf(FuncId func,
                                                     ValueId value) const {
    const auto it = impl_->useLists.find(func);
    if (it == impl_->useLists.end() || value >= it->second.size()) {
        return emptyUses();
    }
    return it->second[value];
}

bool IrBuilder::verify(FuncId func, std::string* errorOut) const {
    return impl_->verifyFunc(func, errorOut, /*requireComplete=*/true);
}

bool IrBuilder::verifyModule(std::string* errorOut) const {
    const IrModule& m = impl_->module;
    for (const auto& [name, funcId] : m.globals) {
        if (funcId >= m.functions.size()) {
            if (errorOut) {
                *errorOut = "symbol global '" + name +
                            "' mereferensikan FuncId di luar tabel";
            }
            return false;
        }
    }
    for (FuncId f = 0; f < m.functions.size(); ++f) {
        if (!impl_->verifyFunc(f, errorOut, /*requireComplete=*/true)) return false;
    }
    return true;
}

bool IrBuilder::Impl::verifyFunc(FuncId func, std::string* errorOut,
                               bool requireComplete) const {
    const IrFunction& fn = module.functions[func];
    std::ostringstream err;

    auto fail = [&](const std::string& message) {
        if (errorOut) {
            *errorOut = "fungsi '" + fn.name + "': " + message;
        }
        return false;
    };

    // (a) InstrId di tiap block valid & setiap instruksi dimuat tepat
    //     satu block (kecuali tombstone pra-compact yang belum dimuat).
    std::vector<int> instrOwner(fn.instrs.size(), -1);
    for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
        const IrBlock& blk = fn.blocks[b];
        for (InstrId id : blk.insts) {
            if (id >= fn.instrs.size()) {
                return fail("block '" + blk.label +
                            "' mereferensikan InstrId di luar tabel");
            }
            if (instrOwner[id] != -1) {
                return fail("instruksi dimuat di lebih dari satu block");
            }
            instrOwner[id] = static_cast<int>(b);
        }
    }
    for (std::size_t i = 0; i < fn.instrs.size(); ++i) {
        if (fn.instrs[i].op != IrOpcode::Dead && instrOwner[i] == -1) {
            return fail("instruksi hidup tidak dimuat di block manapun");
        }
    }

    // (b) Terminator: tiap block tidak kosong, tepat satu, di posisi
    //     terakhir.
    for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
        const IrBlock& blk = fn.blocks[b];
        if (blk.insts.empty()) {
            if (requireComplete) {
                return fail("block '" + blk.label + "' kosong (tanpa terminator)");
            }
            continue;  // block dalam konstruksi: boleh belum berisi apa pun
        }
        for (std::size_t s = 0; s + 1 < blk.insts.size(); ++s) {
            if (isTerminator(fn.instrs[blk.insts[s]].op)) {
                return fail("block '" + blk.label +
                            "' punya terminator di posisi non-terakhir");
            }
        }
        if (requireComplete &&
            !isTerminator(fn.instrs[blk.insts.back()].op)) {
            return fail("block '" + blk.label +
                        "' selesai tanpa terminator (ast.terminator_missing)");
        }
    }

    // (c) Operand ID dalam rentang tabel.
    for (std::size_t i = 0; i < fn.instrs.size(); ++i) {
        const IrInstr& inst = fn.instrs[i];
        for (std::uint32_t slot = 0; slot < inst.operands.size(); ++slot) {
            const IrOperand& op = inst.operands[slot];
            bool ok = true;
            switch (op.kind) {
            case IrOperandKind::ValueV:    ok = op.id < fn.values.size(); break;
            case IrOperandKind::BlockV:    ok = op.id < fn.blocks.size(); break;
            case IrOperandKind::FuncV:     ok = op.id < module.functions.size(); break;
            case IrOperandKind::TypeV:     ok = op.id < module.types.size(); break;
            case IrOperandKind::ConstantV: ok = op.id < module.constants.size(); break;
            case IrOperandKind::ImmV:      ok = true; break;  // inline, tanpa tabel
            case IrOperandKind::GlobalV:   ok = op.id < module.globalVars.size(); break;
            }
            if (!ok) {
                std::ostringstream m;
                m << "instruksi " << i << " operand " << slot
                  << " mereferensikan ID di luar tabel";
                return fail(m.str());
            }
        }
    }

    // (d) Use-list harus identik dengan pemakaian aktual di instruksi.
    {
        const auto it = useLists.find(func);
        if (it != useLists.end() && it->second.size() > fn.values.size()) {
            return fail("use-list lebih panjang dari value table");
        }
        std::vector<std::vector<Use>> expected(fn.values.size());
        for (std::size_t i = 0; i < fn.instrs.size(); ++i) {
            const IrInstr& inst = fn.instrs[i];
            if (inst.op == IrOpcode::Dead) continue;
            for (std::uint32_t slot = 0; slot < inst.operands.size(); ++slot) {
                if (inst.operands[slot].kind == IrOperandKind::ValueV) {
                    expected[inst.operands[slot].id].push_back(
                        {static_cast<InstrId>(i), slot});
                }
            }
        }
        for (std::size_t v = 0; v < fn.values.size(); ++v) {
            std::vector<Use> actual =
                (it != useLists.end() && v < it->second.size())
                    ? it->second[v]
                    : std::vector<Use>();
            std::sort(actual.begin(), actual.end(),
                      [](const Use& a, const Use& b) {
                          return a.instr < b.instr ||
                                 (a.instr == b.instr && a.operandSlot < b.operandSlot);
                      });
            std::sort(expected[v].begin(), expected[v].end(),
                      [](const Use& a, const Use& b) {
                          return a.instr < b.instr ||
                                 (a.instr == b.instr && a.operandSlot < b.operandSlot);
                      });
            if (actual != expected[v]) {
                std::ostringstream m;
                m << "use-list value " << v << " tidak sinkron dengan instruksi";
                return fail(m.str());
            }
        }
    }

    // (e) CFG edges simetris dan sesuai operand BlockV terminator.
    for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
        const IrBlock& blk = fn.blocks[b];
        if (blk.insts.empty()) continue;  // belum dibangun (cek b)
        const IrInstr& last = fn.instrs[blk.insts.back()];
        // Hanya block yang terminator-nya sudah terpasang yang boleh
        // memiliki succs; block dalam konstruksi harus succs kosong.
        std::vector<BlockId> wantSuccs;
        if (isTerminator(last.op)) {
            for (const IrOperand& op : last.operands) {
                if (op.kind == IrOperandKind::BlockV) {
                    wantSuccs.push_back(op.id);
                }
            }
        }
        std::vector<BlockId> haveSuccs = blk.succs;
        std::sort(wantSuccs.begin(), wantSuccs.end());
        std::sort(haveSuccs.begin(), haveSuccs.end());
        if (wantSuccs != haveSuccs) {
            return fail("succs block '" + blk.label +
                        "' tidak sinkron dengan terminator");
        }
        for (BlockId to : blk.succs) {
            const IrBlock& target = fn.blocks[to];
            if (std::find(target.preds.begin(), target.preds.end(),
                          static_cast<BlockId>(b)) == target.preds.end()) {
                return fail("edge tidak simetris: " + blk.label +
                            " -> " + target.label);
            }
        }
    }
    for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
        for (BlockId from : fn.blocks[b].preds) {
            const IrBlock& src = fn.blocks[from];
            if (std::find(src.succs.begin(), src.succs.end(),
                          static_cast<BlockId>(b)) == src.succs.end()) {
                return fail("edge tidak simetris (preds): " + src.label +
                            " -> " + fn.blocks[b].label);
            }
        }
    }

    // (f) Phi: pasangan (BlockV, ValueV) lengkap, pred valid & sesuai
    //     preds block join.
    for (std::size_t i = 0; i < fn.instrs.size(); ++i) {
        const IrInstr& inst = fn.instrs[i];
        if (inst.op != IrOpcode::Phi) continue;
        if (inst.operands.size() % 2 != 0) {
            return fail("phi dengan jumlah operand ganjil");
        }
        const BlockId join = findInstrBlock(fn, static_cast<InstrId>(i));
        if (join == InvalidBlock) continue;  // sudah dilaporkan (a)
        const std::vector<BlockId>& preds = fn.blocks[join].preds;
        if (!requireComplete) {
            continue;  // phi dalam konstruksi: incoming boleh belum lengkap
        }
        if (inst.operands.size() / 2 != preds.size()) {
            std::ostringstream m;
            m << "phi di block '" << fn.blocks[join].label << "' punya "
              << inst.operands.size() / 2 << " incoming, expected "
              << preds.size() << " (ast.phi_insertion_failed)";
            return fail(m.str());
        }
        for (std::size_t k = 0; k < inst.operands.size(); k += 2) {
            if (inst.operands[k].kind != IrOperandKind::BlockV ||
                inst.operands[k + 1].kind != IrOperandKind::ValueV) {
                return fail("phi: pasangan operand bukan (BlockV, ValueV)");
            }
            if (std::find(preds.begin(), preds.end(),
                          inst.operands[k].id) == preds.end()) {
                return fail("phi mereferensikan pred yang tidak ada di join block");
            }
        }
    }

    // (g) Value table: payload valid; value Instr tidak menunjuk
    //     instruksi Dead (invariant internal spesifikasi Bagian 10.4).
    for (std::size_t v = 0; v < fn.values.size(); ++v) {
        const IrValue& value = fn.values[v];
        if (value.type >= module.types.size() && value.type != InvalidType) {
            return fail("value mereferensikan TypeId di luar tabel");
        }
        if (value.kind == IrValue::Kind::Param) {
            if (value.payload >= fn.params.size()) {
                return fail("value Param dengan indeks di luar params");
            }
        } else {
            if (value.payload >= fn.instrs.size()) {
                return fail("value Instr dengan InstrId di luar tabel");
            }
            const IrInstr& producer = fn.instrs[value.payload];
            if (producer.op == IrOpcode::Dead) {
                // Transien legal: markDead sengaja membiarkan value
                // menunjuk tombstone sampai compact membuangnya.
                // Invariant spesifikasi Bagian 10.4 yang keras adalah
                // use-list yang merujuk instruksi Dead (dicek di (d)).
                if (requireComplete) {
                    return fail("value menunjuk instruksi Dead — "
                                "kompaksi belum jalan padahal IR selesai");
                }
            }
            if (producer.resultType != value.type) {
                return fail("tipe value tidak sama dengan resultType penghasil");
            }
        }
    }
    for (std::size_t p = 0; p < fn.params.size(); ++p) {
        const IrValue& pv = fn.values[fn.params[p]];
        if (pv.kind != IrValue::Kind::Param || pv.payload != p) {
            return fail("params tidak sinkron dengan value table");
        }
    }

    // (h) Symbol table lokal menunjuk value valid.
    for (const auto& [name, valueId] : fn.locals) {
        if (valueId >= fn.values.size()) {
            return fail("locals['" + name + "] menunjuk ValueId tidak valid");
        }
    }

    return true;
}

} // namespace rhc
