#include "RhIR/IrProviderManager.h"

#include <utility>

namespace rhc {

namespace {
IrProviderResolution unsupported(std::string_view requested) {
    IrProviderError error{
        IrProviderErrorCode::UnsupportedTarget,
        std::string("target tidak didukung: ") + std::string(requested) +
            "; target yang tersedia: x86_64-linux-sysv",
    };
    return IrProviderResolution{nullptr, std::move(error)};
}
}  // namespace

IrProviderResolution IrProviderManager::resolve() const {
    return IrProviderResolution{&systemVAmd64_, IrProviderError{IrProviderErrorCode::None, {}}};
}

IrProviderResolution IrProviderManager::resolve(std::string_view explicitTarget) const {
    if (explicitTarget.empty()) {
        return IrProviderResolution{
            nullptr,
            IrProviderError{
                IrProviderErrorCode::EmptyExplicitTarget,
                "explicit target tidak boleh kosong",
            },
        };
    }

    if (explicitTarget == "x86_64-linux-sysv") {
        return resolve();
    }
    return unsupported(explicitTarget);
}

}  // namespace rhc
