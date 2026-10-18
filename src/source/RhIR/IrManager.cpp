#include "RhIR/IrManager.h"

#include "RhIR/IrModule.h"

#include <utility>

namespace rhc {

namespace {
IrManagerReport makeError(IrManagerErrorCode code, std::string message) {
    IrManagerReport report;
    report.ok = false;
    report.error = IrManagerError{code, std::move(message)};
    return report;
}

std::string firstVerifyMessage(const IrVerifyReport& report) {
    if (report.errors.empty()) {
        return {};
    }
    return report.errors.front().message;
}
}  // namespace

IrManager::IrManager() : providerOwner_(std::make_shared<IrProviderManager>()) {}

IrManager::IrManager(IrManagerOptions options)
    : options_(std::move(options)),
      providerOwner_(std::make_shared<IrProviderManager>()) {}

const IrModule& IrManager::module() const {
    return *module_;
}

IrModule& IrManager::module() {
    return *module_;
}

IrManagerReport IrManager::adoptModule(std::unique_ptr<IrModule> module) {
    if (!module) {
        return makeError(IrManagerErrorCode::NullModule, "IrModule yang diadopt tidak boleh null");
    }
    if (module_) {
        return makeError(IrManagerErrorCode::ModuleAlreadyAdopted,
                         "IrManager sudah memiliki modul");
    }

    std::shared_ptr<IrModule> adopted(std::move(module));
    IrVerifier verifier;
    // Adopt menjalankan Structural + Type verify: korupsi type table
    // (mis. Int dengan sizeBytes garbage) harus tertangkap SEJAK adopt,
    // bukan baru di finalize/layout.
    IrVerifyReport verifyReport = verifier.verify(*adopted, IrVerifyLevel::Type);
    if (!verifyReport.ok()) {
        IrManagerReport report;
        report.ok = false;
        report.error = IrManagerError{
            IrManagerErrorCode::StructuralVerifyFailed,
            "verify (structural+type) gagal saat adopt: " +
                firstVerifyMessage(verifyReport),
        };
        report.verifyReport = std::move(verifyReport);
        return report;
    }

    module_ = std::move(adopted);
    state_ = IrManagerState::Adopted;

    IrManagerReport report;
    report.ok = true;
    report.verifyReport = std::move(verifyReport);
    return report;
}

IrManagerReport IrManager::finalize() {
    if (!module_) {
        return makeError(IrManagerErrorCode::NotAdopted,
                         "finalize dipanggil sebelum adoptModule");
    }
    if (state_ == IrManagerState::Finalized || state_ == IrManagerState::Frozen) {
        IrManagerReport report;
        report.ok = true;
        return report;
    }
    if (state_ != IrManagerState::Adopted) {
        return makeError(IrManagerErrorCode::NotAdopted, "state manager tidak valid untuk finalize");
    }

    IrManagerReport report;
    IrProviderResolution resolution = options_.explicitTarget.has_value()
                                          ? providerOwner_->resolve(*options_.explicitTarget)
                                          : providerOwner_->resolve();
    report.providerResolution = resolution;
    if (!resolution) {
        report.ok = false;
        report.error = IrManagerError{
            IrManagerErrorCode::ProviderResolutionFailed,
            "provider target gagal: " + resolution.error.message,
        };
        return report;
    }
    targetProvider_ = resolution.provider;

    IrLayoutEngine layoutEngine(*targetProvider_);
    report.layoutReport = layoutEngine.layoutModule(*module_);
    if (!report.layoutReport.ok()) {
        report.ok = false;
        std::string message = "layout engine gagal saat finalize";
        if (!report.layoutReport.errors.empty()) {
            message += ": " + report.layoutReport.errors.front().message;
        }
        report.error = IrManagerError{IrManagerErrorCode::FinalVerifyFailed,
                                      std::move(message)};
        return report;
    }

    IrVerifier verifier;
    report.verifyReport = verifier.verify(*module_, IrVerifyLevel::Final,
                                          targetProvider_,
                                          options_.verifyOptions);
    if (!report.verifyReport.ok()) {
        report.ok = false;
        report.error = IrManagerError{
            IrManagerErrorCode::FinalVerifyFailed,
            "final verify gagal: " + firstVerifyMessage(report.verifyReport),
        };
        return report;
    }

    state_ = IrManagerState::Finalized;
    report.ok = true;
    return report;
}

FinalIr IrManager::freeze() {
    if (state_ == IrManagerState::Frozen) {
        return frozen_;
    }
    if (state_ != IrManagerState::Finalized || !module_ || !targetProvider_) {
        return FinalIr{};
    }

    std::shared_ptr<const IrModule> frozenModule = module_;
    std::shared_ptr<const IrProviderManager> providerOwner = providerOwner_;
    frozen_ = FinalIr(std::move(frozenModule), std::move(providerOwner), targetProvider_);
    state_ = IrManagerState::Frozen;
    return frozen_;
}

}  // namespace rhc
