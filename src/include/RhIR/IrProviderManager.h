#pragma once

#include "RhIR/SystemVAmd64Provider.h"

#include <string>
#include <string_view>

namespace rhc {

enum class IrProviderErrorCode : std::uint8_t {
    None,
    EmptyExplicitTarget,
    UnsupportedTarget,
};

struct IrProviderError {
    IrProviderErrorCode code;
    std::string message;

    constexpr bool ok() const noexcept { return code == IrProviderErrorCode::None; }
    explicit constexpr operator bool() const noexcept { return !ok(); }
};

struct IrProviderResolution {
    const IrTargetProvider* provider;
    IrProviderError error;

    constexpr bool ok() const noexcept { return provider != nullptr && error.ok(); }
    explicit constexpr operator bool() const noexcept { return ok(); }
};

// Manager sempit untuk memilih target provider.
// Sekarang hanya x86_64-linux-sysv yang didukung; target lain harus gagal eksplisit.
class IrProviderManager {
public:
    IrProviderResolution resolve() const;
    IrProviderResolution resolve(std::string_view explicitTarget) const;

private:
    SystemVAmd64Provider systemVAmd64_;
};

}  // namespace rhc
