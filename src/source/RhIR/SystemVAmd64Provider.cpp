#include "RhIR/SystemVAmd64Provider.h"

namespace rhc {

namespace {
constexpr IrDataLayoutDescriptor kSystemVAmd64DataLayout{
    .pointerSizeBytes = 8,
    .pointerAlignmentBytes = 8,
    .maxNaturalAlignmentBytes = 8,
    .endianness = IrEndianness::Little,
};

constexpr IrCallingConventionDescriptor kSystemVAmd64CallingConvention{
    .name = "System V AMD64",
    .stackAlignmentBytes = 16,
};
}  // namespace

const char* SystemVAmd64Provider::name() const noexcept {
    return "Linux AMD64 (X86_64 System V ABI)";
}

IrTargetId SystemVAmd64Provider::targetId() const noexcept {
    return IrTargetId::systemVAmd64();
}

const IrDataLayoutDescriptor& SystemVAmd64Provider::dataLayout() const noexcept {
    return kSystemVAmd64DataLayout;
}

const IrCallingConventionDescriptor& SystemVAmd64Provider::callingConvention() const noexcept {
    return kSystemVAmd64CallingConvention;
}

}  // namespace rhc
