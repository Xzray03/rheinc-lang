#pragma once

#include "RhIR/IrTargetId.h"

#include <cstdint>

namespace rhc {

struct IrDataLayoutDescriptor {
    std::uint8_t pointerSizeBytes;
    std::uint8_t pointerAlignmentBytes;
    std::uint8_t maxNaturalAlignmentBytes;
    IrEndianness endianness;
};

struct IrCallingConventionDescriptor {
    const char* name;
    std::uint8_t stackAlignmentBytes;
};

// Kontrak provider target untuk RhIR.
// Provider tidak melakukan codegen; dia hanya mendeskripsikan target yang dipilih.
class IrTargetProvider {
public:
    virtual ~IrTargetProvider() = default;

    virtual const char* name() const noexcept = 0;
    virtual IrTargetId targetId() const noexcept = 0;
    virtual const IrDataLayoutDescriptor& dataLayout() const noexcept = 0;
    virtual const IrCallingConventionDescriptor& callingConvention() const noexcept = 0;
};

}  // namespace rhc
