#include "RhIR/IrVerifier.h"

#include "RhIR/IrFunction.h"
#include "RhIR/IrLayoutEngine.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrModule.h"

#include <algorithm>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace rhc {

namespace {

void addError(IrVerifyReport& report,
              IrVerifyErrorCode code,
              std::string message,
              FuncId func = InvalidFunc,
              BlockId block = InvalidBlock,
              InstrId instr = InvalidInstr,
              TypeId type = InvalidType,
              std::uint32_t operandIndex = 0) {
    IrVerifyError error;
    error.code = code;
    error.func = func;
    error.block = block;
    error.instr = instr;
    error.type = type;
    error.operandIndex = operandIndex;
    error.message = std::move(message);
    report.errors.push_back(std::move(error));
}

bool validId(std::uint32_t id, std::size_t size) {
    return id != std::numeric_limits<std::uint32_t>::max() &&
           static_cast<std::size_t>(id) < size;
}

void verifyOperand(const IrModule& module,
                   const IrFunction& fn,
                   FuncId funcId,
                   BlockId blockId,
                   InstrId instrId,
                   std::uint32_t operandIndex,
                   const IrOperand& operand,
                   IrVerifyReport& report) {
    bool ok = true;
    switch (operand.kind) {
    case IrOperandKind::ValueV:
        ok = validId(operand.id, fn.values.size());
        break;
    case IrOperandKind::ConstantV:
        ok = validId(operand.id, module.constants.size());
        break;
    case IrOperandKind::BlockV:
        ok = validId(operand.id, fn.blocks.size());
        break;
    case IrOperandKind::FuncV:
        ok = validId(operand.id, module.functions.size());
        break;
    case IrOperandKind::TypeV:
        ok = validId(operand.id, module.types.size());
        break;
    case IrOperandKind::ImmV:
        ok = true;
        break;
    case IrOperandKind::GlobalV:
        ok = validId(operand.id, module.globalVars.size());
        break;
    }

    if (!ok) {
        addError(report, IrVerifyErrorCode::OperandOutOfRange,
                 "operand id di luar tabel terkait", funcId, blockId, instrId,
                 InvalidType, operandIndex);
    }
}

void verifyFunction(const IrModule& module,
                    FuncId funcId,
                    std::map<std::string, FuncId>& functionNames,
                    IrVerifyReport& report) {
    const IrFunction& fn = module.functions[funcId];

    if (!validId(fn.fnType, module.types.size())) {
        if (fn.kind != IrFunctionKind::External) {
            addError(report, IrVerifyErrorCode::InvalidFunctionType,
                     "fnType fungsi non-External invalid", funcId, InvalidBlock,
                     InvalidInstr, fn.fnType);
        }
    } else if (module.types[fn.fnType].kind != IrTypeKind::FnPtr) {
        addError(report, IrVerifyErrorCode::InvalidFunctionType,
                 "fnType fungsi bukan FnPtr", funcId, InvalidBlock, InvalidInstr, fn.fnType);
    }

    if (fn.name.empty()) {
        addError(report, IrVerifyErrorCode::EmptyFunctionName,
                 "nama fungsi kosong", funcId);
    } else if (const auto [it, inserted] = functionNames.emplace(fn.name, funcId); !inserted) {
        addError(report, IrVerifyErrorCode::DuplicateFunctionName,
                 "nama fungsi duplikat: " + fn.name, funcId);
    }

    for (const auto& [localName, valueId] : fn.locals) {
        (void)localName;
        if (!validId(valueId, fn.values.size())) {
            addError(report, IrVerifyErrorCode::InvalidLocalValue,
                     "locals memetakan nama ke ValueId invalid", funcId);
        }
    }

    std::vector<BlockId> instructionOwner(fn.instrs.size(), InvalidBlock);
    for (BlockId blockId = 0; blockId < fn.blocks.size(); ++blockId) {
        const IrBlock& block = fn.blocks[blockId];
        if (block.insts.empty()) {
            addError(report, IrVerifyErrorCode::EmptyBlock,
                     "block kosong", funcId, blockId);
            continue;
        }

        for (std::size_t slot = 0; slot < block.insts.size(); ++slot) {
            const InstrId instrId = block.insts[slot];
            if (!validId(instrId, fn.instrs.size())) {
                addError(report, IrVerifyErrorCode::InvalidBlockInstruction,
                         "block memuat InstrId invalid", funcId, blockId, instrId);
                continue;
            }
            if (instructionOwner[instrId] != InvalidBlock) {
                addError(report, IrVerifyErrorCode::MultipleBlockOwner,
                         "instruksi dimuat lebih dari satu block", funcId, blockId, instrId);
                continue;
            }
            instructionOwner[instrId] = blockId;

            const IrInstr& instr = fn.instrs[instrId];
            if (isTerminator(instr.op) && slot + 1 != block.insts.size()) {
                addError(report, IrVerifyErrorCode::TerminatorNotLast,
                         "terminator bukan instruksi terakhir block", funcId, blockId, instrId);
            }

            for (std::uint32_t operandIndex = 0;
                 operandIndex < instr.operands.size();
                 ++operandIndex) {
                verifyOperand(module, fn, funcId, blockId, instrId, operandIndex,
                              instr.operands[operandIndex], report);
            }
        }

        const IrInstr& terminator = fn.instrs[block.insts.back()];
        if (!isTerminator(terminator.op)) {
            addError(report, IrVerifyErrorCode::MissingTerminator,
                     "block tidak diakhiri terminator", funcId, blockId,
                     block.insts.back());
            continue;
        }

        std::vector<BlockId> referencedBlocks;
        for (const IrOperand& operand : terminator.operands) {
            if (operand.kind == IrOperandKind::BlockV) {
                referencedBlocks.push_back(static_cast<BlockId>(operand.id));
            }
        }
        std::sort(referencedBlocks.begin(), referencedBlocks.end());
        referencedBlocks.erase(std::unique(referencedBlocks.begin(), referencedBlocks.end()),
                               referencedBlocks.end());

        std::vector<BlockId> sortedSuccs = block.succs;
        std::sort(sortedSuccs.begin(), sortedSuccs.end());
        sortedSuccs.erase(std::unique(sortedSuccs.begin(), sortedSuccs.end()),
                          sortedSuccs.end());
        if (referencedBlocks != sortedSuccs) {
            addError(report, IrVerifyErrorCode::SuccPredMismatch,
                     "succs block tidak cocok dengan block operand terminator",
                     funcId, blockId, block.insts.back());
        }

        for (const BlockId succ : block.succs) {
            if (!validId(succ, fn.blocks.size())) {
                continue;
            }
            const auto& preds = fn.blocks[succ].preds;
            if (std::find(preds.begin(), preds.end(), blockId) == preds.end()) {
                addError(report, IrVerifyErrorCode::SuccPredMismatch,
                         "succ tidak memiliki pred balik", funcId, blockId,
                         block.insts.back());
            }
        }
    }

    for (InstrId instrId = 0; instrId < fn.instrs.size(); ++instrId) {
        if (instructionOwner[instrId] == InvalidBlock) {
            addError(report, IrVerifyErrorCode::InstructionNotInBlock,
                     "instruksi tidak dimuat block manapun", funcId, InvalidBlock, instrId);
        }
    }

    std::vector<ValueId> valueByInstr(fn.instrs.size(), InvalidValue);
    for (ValueId valueId = 0; valueId < fn.values.size(); ++valueId) {
        const IrValue& value = fn.values[valueId];
        if (!validId(value.type, module.types.size())) {
            addError(report, IrVerifyErrorCode::InvalidValueType,
                     "value.type invalid", funcId, InvalidBlock, InvalidInstr, value.type);
        }

        if (value.kind == IrValue::Kind::Param) {
            if (value.payload >= fn.params.size() || fn.params[value.payload] != valueId) {
                addError(report, IrVerifyErrorCode::InvalidValuePayload,
                         "payload Param tidak konsisten dengan params", funcId);
            }
            continue;
        }

        if (value.payload >= fn.instrs.size()) {
            addError(report, IrVerifyErrorCode::InvalidValuePayload,
                     "payload Instr di luar tabel instruksi", funcId, InvalidBlock,
                     static_cast<InstrId>(value.payload));
            continue;
        }

        const InstrId producer = static_cast<InstrId>(value.payload);
        const IrInstr& instr = fn.instrs[producer];
        if (instr.resultType == InvalidType) {
            addError(report, IrVerifyErrorCode::OrphanResultValue,
                     "value Instr menunjuk instruksi tanpa result", funcId, InvalidBlock,
                     producer, value.type);
            continue;
        }
        if (instr.resultType != value.type) {
            addError(report, IrVerifyErrorCode::ResultTypeMismatch,
                     "value.type tidak sama dengan resultType instruksi penghasil",
                     funcId, InvalidBlock, producer, value.type);
        }
        valueByInstr[producer] = valueId;
    }

    for (InstrId instrId = 0; instrId < fn.instrs.size(); ++instrId) {
        if (fn.instrs[instrId].resultType != InvalidType && valueByInstr[instrId] == InvalidValue) {
            addError(report, IrVerifyErrorCode::MissingResultValue,
                     "instruksi bertipe result tidak punya value", funcId, InvalidBlock,
                     instrId, fn.instrs[instrId].resultType);
        }
    }

    for (std::uint32_t paramIndex = 0; paramIndex < fn.params.size(); ++paramIndex) {
        const ValueId valueId = fn.params[paramIndex];
        if (!validId(valueId, fn.values.size())) {
            addError(report, IrVerifyErrorCode::InvalidParamValue,
                     "params memuat ValueId invalid", funcId);
            continue;
        }
        const IrValue& value = fn.values[valueId];
        if (value.kind != IrValue::Kind::Param || value.payload != paramIndex) {
            addError(report, IrVerifyErrorCode::InvalidParamValue,
                     "params tidak konsisten dengan value Param", funcId);
        }
    }
}


bool isKnownTypeKind(IrTypeKind kind) {
    return static_cast<std::uint8_t>(kind) <=
           static_cast<std::uint8_t>(IrTypeKind::FnPtr);
}

void requireTypeId(const IrModule& module,
                   TypeId id,
                   TypeId ownerType,
                   IrVerifyErrorCode code,
                   const char* what,
                   IrVerifyReport& report) {
    if (!validId(id, module.types.size())) {
        addError(report, code, std::string(what) + " invalid", InvalidFunc,
                 InvalidBlock, InvalidInstr, ownerType);
    }
}

void verifyTypeModule(const IrModule& module, IrVerifyReport& report) {
    for (TypeId typeId = 0; typeId < module.types.size(); ++typeId) {
        const IrType& type = module.types[typeId];
        if (!isKnownTypeKind(type.kind)) {
            addError(report, IrVerifyErrorCode::InvalidTypeKind,
                     "IrTypeKind tidak dikenal", InvalidFunc, InvalidBlock,
                     InvalidInstr, typeId);
            continue;
        }

        switch (type.kind) {
        case IrTypeKind::Void:
        case IrTypeKind::Bool:
            break;
        case IrTypeKind::Int:
            if (type.sizeBytes != 1 && type.sizeBytes != 2 &&
                type.sizeBytes != 4 && type.sizeBytes != 8) {
                addError(report, IrVerifyErrorCode::InvalidTypeShape,
                         "integer sizeBytes harus 1, 2, 4, atau 8", InvalidFunc,
                         InvalidBlock, InvalidInstr, typeId);
            }
            break;
        case IrTypeKind::Float:
            if (type.sizeBytes != 4 && type.sizeBytes != 8) {
                addError(report, IrVerifyErrorCode::InvalidTypeShape,
                         "float sizeBytes harus 4 atau 8", InvalidFunc,
                         InvalidBlock, InvalidInstr, typeId);
            }
            break;
        case IrTypeKind::Pointer:
        case IrTypeKind::Array:
        case IrTypeKind::View:
            requireTypeId(module, type.elementType, typeId,
                          IrVerifyErrorCode::InvalidTypeReference,
                          "elementType", report);
            break;
        case IrTypeKind::Struct:
            for (const IrField& field : type.fields) {
                requireTypeId(module, field.type, typeId,
                              IrVerifyErrorCode::InvalidTypeReference,
                              "field struct", report);
            }
            break;
        case IrTypeKind::Sum:
            for (const IrVariant& variant : type.variants) {
                if (variant.payloadType != InvalidType) {
                    requireTypeId(module, variant.payloadType, typeId,
                                  IrVerifyErrorCode::InvalidTypeReference,
                                  "payload sum", report);
                }
            }
            break;
        case IrTypeKind::FnPtr:
            requireTypeId(module, type.returnType, typeId,
                          IrVerifyErrorCode::InvalidTypeReference,
                          "returnType fnptr", report);
            for (const TypeId param : type.paramTypes) {
                requireTypeId(module, param, typeId,
                              IrVerifyErrorCode::InvalidTypeReference,
                              "paramType fnptr", report);
            }
            break;
        }
    }

    for (ConstId constId = 0; constId < module.constants.size(); ++constId) {
        if (!validId(module.constants[constId].type, module.types.size())) {
            addError(report, IrVerifyErrorCode::InvalidConstantType,
                     "IrConstant.type invalid", InvalidFunc, InvalidBlock,
                     InvalidInstr, module.constants[constId].type);
        }
    }

    for (std::uint32_t globalIndex = 0; globalIndex < module.globalVars.size();
         ++globalIndex) {
        if (!validId(module.globalVars[globalIndex].type, module.types.size())) {
            addError(report, IrVerifyErrorCode::InvalidGlobalVarType,
                     "IrGlobal.type invalid", InvalidFunc, InvalidBlock,
                     InvalidInstr, module.globalVars[globalIndex].type);
        }
    }

    for (FuncId funcId = 0; funcId < module.functions.size(); ++funcId) {
        const IrFunction& fn = module.functions[funcId];
        if (validId(fn.fnType, module.types.size())) {
            if (module.types[fn.fnType].kind != IrTypeKind::FnPtr) {
                addError(report, IrVerifyErrorCode::InvalidFunctionType,
                         "fnType fungsi bukan FnPtr", funcId, InvalidBlock,
                         InvalidInstr, fn.fnType);
            }
        } else if (fn.kind != IrFunctionKind::External) {
            addError(report, IrVerifyErrorCode::InvalidFunctionType,
                     "fnType fungsi non-External invalid", funcId, InvalidBlock,
                     InvalidInstr, fn.fnType);
        }

        for (const IrInstr& instr : fn.instrs) {
            if (instr.resultType != InvalidType &&
                !validId(instr.resultType, module.types.size())) {
                addError(report, IrVerifyErrorCode::InvalidInstructionResultType,
                         "resultType instruksi invalid", funcId, InvalidBlock,
                         InvalidInstr, instr.resultType);
            }
        }
    }
}

void compareExpectedLayout(const IrModule& actual,
                           const IrModule& expected,
                           IrVerifyReport& report) {
    for (TypeId typeId = 0; typeId < actual.types.size(); ++typeId) {
        const IrType& got = actual.types[typeId];
        const IrType& want = expected.types[typeId];
        bool mismatch = got.sizeBytes != want.sizeBytes ||
                        got.alignBytes != want.alignBytes;
        if (!mismatch && got.kind == IrTypeKind::Struct) {
            for (std::size_t fieldIndex = 0; fieldIndex < got.fields.size();
                 ++fieldIndex) {
                if (got.fields[fieldIndex].offsetBytes !=
                    want.fields[fieldIndex].offsetBytes) {
                    mismatch = true;
                    break;
                }
            }
        }
        if (mismatch) {
            addError(report, IrVerifyErrorCode::LayoutMismatch,
                     "layout tipe tidak cocok dengan layout System V AMD64",
                     InvalidFunc, InvalidBlock, InvalidInstr, typeId);
        }
    }
}

void verifyLayoutModule(const IrModule& module,
                        const IrTargetProvider& provider,
                        IrVerifyReport& report) {
    IrModule expected = module;
    IrLayoutEngine layoutEngine(provider);
    const IrLayoutReport layoutReport = layoutEngine.layoutModule(expected);
    if (!layoutReport.ok()) {
        std::string message = "layout engine gagal";
        if (!layoutReport.errors.empty()) {
            message += ": " + layoutReport.errors.front().message;
        }
        addError(report, IrVerifyErrorCode::LayoutMismatch, std::move(message));
        return;
    }
    compareExpectedLayout(module, expected, report);
}

}  // namespace



bool isKnownFunctionKind(IrFunctionKind kind) {
    return static_cast<std::uint8_t>(kind) <=
           static_cast<std::uint8_t>(IrFunctionKind::Entry);
}

bool isVoidFnPtr(const IrModule& module, TypeId fnType, bool requireNoParams) {
    if (!validId(fnType, module.types.size())) {
        return false;
    }
    const IrType& type = module.types[fnType];
    if (type.kind != IrTypeKind::FnPtr) {
        return false;
    }
    if (!validId(type.returnType, module.types.size()) ||
        module.types[type.returnType].kind != IrTypeKind::Void) {
        return false;
    }
    return !requireNoParams || type.paramTypes.empty();
}

void verifyFinalModule(const IrModule& module, IrVerifyReport& report) {
    std::uint32_t entryCount = 0;
    std::uint32_t globalInitCount = 0;

    for (FuncId funcId = 0; funcId < module.functions.size(); ++funcId) {
        const IrFunction& fn = module.functions[funcId];
        if (!isKnownFunctionKind(fn.kind)) {
            addError(report, IrVerifyErrorCode::InvalidFunctionKind,
                     "IrFunctionKind tidak dikenal", funcId);
            continue;
        }

        switch (fn.kind) {
        case IrFunctionKind::External:
            if (!fn.blocks.empty()) {
                addError(report, IrVerifyErrorCode::ExternalFunctionWithBody,
                         "fungsi External tidak boleh punya body", funcId);
            }
            break;
        case IrFunctionKind::Normal:
            if (fn.blocks.empty()) {
                addError(report, IrVerifyErrorCode::FunctionMissingBody,
                         "fungsi Normal wajib punya body", funcId);
            }
            break;
        case IrFunctionKind::GlobalInit:
            ++globalInitCount;
            if (fn.blocks.empty()) {
                addError(report, IrVerifyErrorCode::FunctionMissingBody,
                         ".__global_init wajib punya body", funcId);
            }
            if (!isVoidFnPtr(module, fn.fnType, true)) {
                addError(report, IrVerifyErrorCode::InvalidGlobalInitFunction,
                         ".__global_init harus fnptr () -> void", funcId, InvalidBlock,
                         InvalidInstr, fn.fnType);
            }
            break;
        case IrFunctionKind::Entry:
            ++entryCount;
            if (fn.blocks.empty()) {
                addError(report, IrVerifyErrorCode::FunctionMissingBody,
                         "entry function wajib punya body", funcId);
            }
            if (!validId(fn.fnType, module.types.size()) ||
                module.types[fn.fnType].kind != IrTypeKind::FnPtr) {
                addError(report, IrVerifyErrorCode::InvalidEntryFunction,
                         "entry function harus bertipe FnPtr", funcId, InvalidBlock,
                         InvalidInstr, fn.fnType);
            }
            break;
        }
    }

    if (entryCount == 0) {
        addError(report, IrVerifyErrorCode::MissingEntryFunction,
                 "entry function tidak ditemukan");
    } else if (entryCount > 1) {
        addError(report, IrVerifyErrorCode::DuplicateEntryFunction,
                 "entry function lebih dari satu");
    }
    if (globalInitCount > 1) {
        addError(report, IrVerifyErrorCode::DuplicateGlobalInit,
                 ".__global_init lebih dari satu");
    }
}

void verifyStructuralModule(const IrModule& module, IrVerifyReport& report) {
    for (const auto& [symbolName, funcId] : module.globals) {
        if (symbolName.empty() || !validId(funcId, module.functions.size())) {
            addError(report, IrVerifyErrorCode::InvalidGlobalSymbol,
                     "symbol global invalid: " + symbolName);
        }
    }

    std::map<std::string, FuncId> functionNames;
    for (FuncId funcId = 0; funcId < module.functions.size(); ++funcId) {
        verifyFunction(module, funcId, functionNames, report);
    }
}

IrVerifyReport IrVerifier::verify(const IrModule& module,
                                  IrVerifyLevel level,
                                  const IrTargetProvider* targetProvider) const {
    IrVerifyReport report;

    switch (level) {
    case IrVerifyLevel::Structural:
        verifyStructuralModule(module, report);
        break;
    case IrVerifyLevel::Type:
        verifyStructuralModule(module, report);
        verifyTypeModule(module, report);
        break;
    case IrVerifyLevel::Layout:
        verifyStructuralModule(module, report);
        verifyTypeModule(module, report);
        if (targetProvider == nullptr) {
            addError(report, IrVerifyErrorCode::MissingTargetProvider,
                     "layout verify memerlukan IrTargetProvider");
        } else {
            verifyLayoutModule(module, *targetProvider, report);
        }
        break;
    case IrVerifyLevel::Final:
        verifyStructuralModule(module, report);
        verifyTypeModule(module, report);
        if (targetProvider == nullptr) {
            addError(report, IrVerifyErrorCode::MissingTargetProvider,
                     "final verify memerlukan IrTargetProvider");
        } else {
            verifyLayoutModule(module, *targetProvider, report);
            verifyFinalModule(module, report);
        }
        break;
    }
    return report;
}

}  // namespace rhc
