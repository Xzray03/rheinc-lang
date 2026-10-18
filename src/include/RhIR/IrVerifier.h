#pragma once

#include "RhIR/IrIds.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rhc {

class IrModule;

enum class IrVerifyLevel : std::uint8_t {
    Structural,
    Type,
    Layout,
    Final,
};

// Kebijakan entry function saat verify Final. Default ExactlyOne menjaga
// perilaku lama; RhMain mengirim status file (target executable/object dan
// file utama/bukan) lalu memilih policy ini tanpa mengubah IR.
enum class IrEntryPolicy : std::uint8_t {
    ExactlyOne,  // file utama executable: entry wajib tepat satu
    None,        // object / file non-utama executable: entry dilarang
};

struct IrVerifyOptions {
    IrEntryPolicy entryPolicy = IrEntryPolicy::ExactlyOne;
};

enum class IrVerifyErrorCode : std::uint8_t {
    None,
    UnsupportedVerifyLevel,
    InvalidTypeKind,
    InvalidTypeShape,
    InvalidTypeReference,
    InvalidConstantType,
    InvalidGlobalVarType,
    InvalidInstructionResultType,
    MissingTargetProvider,
    LayoutMismatch,
    InvalidFunctionKind,
    MissingEntryFunction,
    DuplicateEntryFunction,
    DefDoesNotDominateUse,   // operand ValueV: def tak mendominasi use
    InvalidEntryFunction,
    UnexpectedEntryFunction,
    ExternalFunctionWithBody,
    FunctionMissingBody,
    DuplicateGlobalInit,
    InvalidGlobalInitFunction,
    InvalidFunctionType,
    EmptyFunctionName,
    DuplicateFunctionName,
    InvalidGlobalSymbol,
    EmptyBlock,
    InvalidBlockInstruction,
    InstructionNotInBlock,
    MultipleBlockOwner,
    TerminatorNotLast,
    MissingTerminator,
    OperandOutOfRange,
    InvalidParamValue,
    InvalidLocalValue,
    InvalidValueType,
    InvalidValuePayload,
    MissingResultValue,
    OrphanResultValue,
    ResultTypeMismatch,
    SuccPredMismatch,
};

struct IrVerifyError {
    IrVerifyErrorCode code;
    FuncId func = InvalidFunc;
    BlockId block = InvalidBlock;
    InstrId instr = InvalidInstr;
    TypeId type = InvalidType;
    std::uint32_t operandIndex = 0;
    std::string message;
};

struct IrVerifyReport {
    std::vector<IrVerifyError> errors;

    bool ok() const noexcept { return errors.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Verifier IR Level Structural untuk data model IrModule.
// Level Type/Layout/Final belum diimplementasikan dan akan ditolak eksplisit.
class IrTargetProvider;

class IrVerifier {
public:
    // level bersifat kumulatif:
    // Type = Structural + type check; Layout = Structural + Type + layout check.
    IrVerifyReport verify(const IrModule& module,
                          IrVerifyLevel level,
                          const IrTargetProvider* targetProvider = nullptr,
                          const IrVerifyOptions& options = {}) const;
};

}  // namespace rhc
