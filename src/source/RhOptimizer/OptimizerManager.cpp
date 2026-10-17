#include "RhOptimizer/OptimizerManager.h"

#include "RhIR/IrEditor.h"
#include "RhIR/IrModule.h"
#include "RhIR/IrVerifier.h"

#include <unordered_map>

namespace rhc {

struct OptimizerManager::Impl {
    explicit Impl(OptimizerOptions opts) : options(opts) {}

    OptimizerOptions options;
    std::vector<std::unique_ptr<IrPass>> order;  // urutan eksekusi
    std::unordered_map<std::string, IrPass*> byName;
};

OptimizerManager::OptimizerManager()
    : impl_(new Impl(OptimizerOptions{})) {}

OptimizerManager::OptimizerManager(OptimizerOptions options)
    : impl_(new Impl(options)) {}

OptimizerManager::~OptimizerManager() = default;

bool OptimizerManager::registerPass(std::unique_ptr<IrPass> pass) {
    if (!pass) return false;
    const std::string key{pass->name()};
    if (impl_->byName.count(key) != 0) return false;  // duplikat
    IrPass* raw = pass.get();
    impl_->order.push_back(std::move(pass));
    impl_->byName.emplace(key, raw);
    return true;
}

bool OptimizerManager::hasPass(std::string_view name) const {
    return impl_->byName.count(std::string(name)) != 0;
}

std::vector<std::string> OptimizerManager::passNames() const {
    std::vector<std::string> names;
    names.reserve(impl_->order.size());
    for (const auto& p : impl_->order) names.emplace_back(p->name());
    return names;
}

OptimizerReport OptimizerManager::runPass(std::string_view name,
                                          IrModule& module) {
    OptimizerReport report;
    const auto it = impl_->byName.find(std::string(name));
    if (it == impl_->byName.end()) {
        report.errorMessage = "pass tidak terdaftar: " + std::string(name);
        return report;
    }
    const std::uint64_t epochBefore = module.epoch;
    IrPassResult result = it->second->run(module);
    result.epochDelta = module.epoch - epochBefore;
    // Change-detection net: IrModule::epoch bumped oleh setiap mutasi
    // IrEditor — klaim `changed` pass harus konsisten dengan kenyataan.
    if (result.changed && result.epochDelta == 0) {
        report.results.emplace_back(std::string(name), result);
        report.errorMessage = "pass '" + std::string(name) +
            "' melaporkan changed=true tetapi tidak ada mutasi IR "
            "(epoch delta 0) — klaim changed tidak jujur";
        return report;
    }
    if (!result.changed && result.epochDelta != 0) {
        report.results.emplace_back(std::string(name), result);
        report.errorMessage = "pass '" + std::string(name) +
            "' mengubah IR (epoch delta " +
            std::to_string(result.epochDelta) +
            ") tanpa melaporkan changed=true";
        return report;
    }
    if (!result.ok) {
        report.results.emplace_back(std::string(name), result);
        report.errorMessage = "pass gagal: " + std::string(name);
        return report;
    }
    if (impl_->options.verifyEachPass && result.changed) {
        const IrVerifyReport verifyReport =
            IrVerifier{}.verify(module, IrVerifyLevel::Structural);
        if (!verifyReport.ok()) {
            report.results.emplace_back(std::string(name), result);
            report.errorMessage = "verify gagal setelah pass '" +
                std::string(name) + "': " +
                (verifyReport.errors.empty()
                     ? std::string("unknown")
                     : verifyReport.errors.front().message);
            return report;
        }
    }
    report.ok = true;
    report.results.emplace_back(std::string(name), result);
    return report;
}

OptimizerReport OptimizerManager::runAll(IrModule& module) {
    OptimizerReport report;
    report.ok = true;
    const std::uint32_t maxIter =
        impl_->options.iterateToFixpoint ? impl_->options.maxIterations : 1;
    for (std::uint32_t iter = 0; iter < maxIter; ++iter) {
        bool anyChanged = false;
        for (const auto& pass : impl_->order) {
            const std::string name{pass->name()};
            OptimizerReport step = runPass(name, module);
            report.results.insert(report.results.end(),
                                  step.results.begin(), step.results.end());
            if (!step.ok) {
                report.ok = false;
                report.errorMessage = step.errorMessage;
                return report;  // fail-fast
            }
            anyChanged = anyChanged || step.results.back().second.changed;
        }
        if (!impl_->options.iterateToFixpoint || !anyChanged) {
            return report;  // konvergen (atau iterasi tunggal diminta)
        }
    }
    report.ok = false;
    report.errorMessage = "optimizer tidak konvergen setelah " +
        std::to_string(maxIter) + " iterasi";
    return report;
}

} // namespace rhc
