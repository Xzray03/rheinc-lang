#pragma once

#include "RhIR/IrIds.h"
#include "RhIR/IrTargetProvider.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rhc {

class IrModule;

enum class IrLayoutErrorCode : std::uint8_t {
    None,
    InvalidTypeId,
    InvalidIntegerSize,
    InvalidFloatSize,
    RecursiveByValue,
    UnsupportedBitfield,
    InvalidArrayElement,
    InvalidPointerElement,
    InvalidViewElement,
    InvalidFunctionSignature,
    LayoutTooLarge,
};

struct IrLayoutError {
    IrLayoutErrorCode code;
    TypeId type = InvalidType;
    std::string message;
};

struct IrLayoutReport {
    std::vector<IrLayoutError> errors;

    bool ok() const noexcept { return errors.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Menghitung layout tipe IR memakai provider target.
// Fungsi ini bersifat all-or-nothing: kalau ada error, IrModule tidak diubah.
class IrLayoutEngine {
public:
    explicit IrLayoutEngine(const IrTargetProvider& provider) : provider_(provider) {}

    IrLayoutReport layoutModule(IrModule& module) const;

private:
    const IrTargetProvider& provider_;
};

}  // namespace rhc
