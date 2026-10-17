#pragma once

#include "RhIR/FinalIr.h"
#include "RhIR/IrLayoutEngine.h"
#include "RhIR/IrProviderManager.h"
#include "RhIR/IrVerifier.h"

#include <memory>
#include <optional>
#include <string>

namespace rhc {

class IrModule;

enum class IrManagerState : std::uint8_t {
    Empty,
    Adopted,
    Finalized,
    Frozen,
};

enum class IrManagerErrorCode : std::uint8_t {
    None,
    NullModule,
    ModuleAlreadyAdopted,
    NotAdopted,
    ProviderResolutionFailed,
    StructuralVerifyFailed,
    FinalVerifyFailed,
};

struct IrManagerError {
    IrManagerErrorCode code;
    std::string message;
};

struct IrManagerReport {
    bool ok = false;
    IrManagerError error{IrManagerErrorCode::None, {}};
    IrProviderResolution providerResolution{nullptr, {IrProviderErrorCode::None, {}}};
    IrLayoutReport layoutReport;
    IrVerifyReport verifyReport;
};

struct IrManagerOptions {
    // Jika nullopt, provider manager memakai default x86_64-linux-sysv.
    std::optional<std::string> explicitTarget;
};

// Lifecycle manager IR:
// adopt -> finalize -> freeze -> FinalIr.
// Normalize/pass mutasi disiapkan sebagai tahapan berikutnya setelah IrEditor ada.
class IrManager {
public:
    IrManager();
    explicit IrManager(IrManagerOptions options);

    IrManager(const IrManager&) = delete;
    IrManager& operator=(const IrManager&) = delete;
    IrManager(IrManager&&) = delete;
    IrManager& operator=(IrManager&&) = delete;

    IrManagerReport adoptModule(std::unique_ptr<IrModule> module);
    IrManagerReport finalize();

    bool hasModule() const noexcept { return module_ != nullptr; }
    const IrModule& module() const;
    // Akses mutable untuk pass optimizer yang berjalan antara adopt
    // dan finalize. Setelah finalize/freeze modul dikonsumsi dan query
    // ini tidak tersedia (modul null) -> null deref bila disalahgunakan;
    // batasi pemakaian pada state Adopted.
    IrModule& module();
    IrManagerState state() const noexcept { return state_; }

    // Valid hanya setelah finalize berhasil.
    FinalIr freeze();

private:
    IrManagerOptions options_;
    std::shared_ptr<IrProviderManager> providerOwner_;
    const IrTargetProvider* targetProvider_ = nullptr;
    std::shared_ptr<IrModule> module_;
    IrManagerState state_ = IrManagerState::Empty;
    FinalIr frozen_;
};

}  // namespace rhc
