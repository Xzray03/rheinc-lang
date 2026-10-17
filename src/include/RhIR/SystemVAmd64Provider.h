#pragma once

#include "RhIR/IrTargetProvider.h"

namespace rhc {

class SystemVAmd64Provider final : public IrTargetProvider {
public:
    const char* name() const noexcept override;
    IrTargetId targetId() const noexcept override;
    const IrDataLayoutDescriptor& dataLayout() const noexcept override;
    const IrCallingConventionDescriptor& callingConvention() const noexcept override;
};

}  // namespace rhc
