#include "RhIR/FinalIr.h"

#include "RhIR/IrModule.h"

#include <utility>

namespace rhc {

FinalIr::FinalIr(std::shared_ptr<const IrModule> module,
                 std::shared_ptr<const IrProviderManager> providerOwner,
                 const IrTargetProvider* provider)
    : module_(std::move(module)),
      providerOwner_(std::move(providerOwner)),
      provider_(provider) {
    if (!module_ || !provider_) {
        return;
    }

    for (FuncId funcId = 0; funcId < module_->functions.size(); ++funcId) {
        const IrFunction& fn = module_->functions[funcId];
        if (!fn.name.empty()) {
            functionByName_.emplace(fn.name, funcId);
        }
        if (fn.kind == IrFunctionKind::External) {
            externalFunctions_.push_back(funcId);
            if (fn.isBuiltin) {
                builtinExternalFunctions_.push_back(funcId);
            } else {
                userExternalFunctions_.push_back(funcId);
            }
        } else if (fn.kind == IrFunctionKind::Entry) {
            entryFunction_ = funcId;
        } else if (fn.kind == IrFunctionKind::GlobalInit) {
            globalInitFunction_ = funcId;
        }
    }

    for (std::uint32_t globalIndex = 0; globalIndex < module_->globalVars.size();
         ++globalIndex) {
        const IrGlobal& global = module_->globalVars[globalIndex];
        if (!global.name.empty()) {
            globalByName_.emplace(global.name, globalIndex);
        }
    }
}

const IrModule& FinalIr::module() const {
    return *module_;
}

const IrTargetProvider& FinalIr::targetProvider() const {
    return *provider_;
}

IrTargetId FinalIr::targetId() const {
    return provider_->targetId();
}

std::uint32_t FinalIr::formatVersion() const {
    return module_->irVersion;
}

const IrFunction* FinalIr::functionByName(std::string_view name) const {
    const auto it = functionByName_.find(std::string(name));
    if (it == functionByName_.end()) {
        return nullptr;
    }
    return &module_->functions[it->second];
}

const IrGlobal* FinalIr::globalByName(std::string_view name) const {
    const auto it = globalByName_.find(std::string(name));
    if (it == globalByName_.end()) {
        return nullptr;
    }
    return &module_->globalVars[it->second];
}

const IrType* FinalIr::layoutOf(TypeId type) const {
    if (type == InvalidType || static_cast<std::size_t>(type) >= module_->types.size()) {
        return nullptr;
    }
    return &module_->types[type];
}

FuncId FinalIr::entryFunction() const {
    return entryFunction_;
}

FuncId FinalIr::globalInitFunction() const {
    return globalInitFunction_;
}

const std::vector<FuncId>& FinalIr::externalFunctions() const {
    return externalFunctions_;
}

const std::vector<FuncId>& FinalIr::builtinExternalFunctions() const {
    return builtinExternalFunctions_;
}

const std::vector<FuncId>& FinalIr::userExternalFunctions() const {
    return userExternalFunctions_;
}

}  // namespace rhc
