#pragma once

#include "RhIR/IrFunction.h"
#include "RhIR/IrProviderManager.h"
#include "RhIR/IrTargetId.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rhc {

class IrModule;
struct IrGlobal;
struct IrType;

// Read-only/frozen view IR Final untuk konsumen seperti RhBackend.
// Objek ini menjaga modul dan provider owner tetap hidup.
class FinalIr {
public:
    FinalIr() = default;
    FinalIr(std::shared_ptr<const IrModule> module,
            std::shared_ptr<const IrProviderManager> providerOwner,
            const IrTargetProvider* provider);

    bool valid() const noexcept { return module_ != nullptr && provider_ != nullptr; }
    explicit operator bool() const noexcept { return valid(); }

    const IrModule& module() const;
    const IrTargetProvider& targetProvider() const;
    IrTargetId targetId() const;
    std::uint32_t formatVersion() const;

    const IrFunction* functionByName(std::string_view name) const;
    const IrGlobal* globalByName(std::string_view name) const;
    const IrType* layoutOf(TypeId type) const;

    FuncId entryFunction() const;
    FuncId globalInitFunction() const;
    const std::vector<FuncId>& externalFunctions() const;

private:
    std::shared_ptr<const IrModule> module_;
    std::shared_ptr<const IrProviderManager> providerOwner_;
    const IrTargetProvider* provider_ = nullptr;

    std::map<std::string, FuncId> functionByName_;
    std::map<std::string, std::uint32_t> globalByName_;
    std::vector<FuncId> externalFunctions_;
    FuncId entryFunction_ = InvalidFunc;
    FuncId globalInitFunction_ = InvalidFunc;
};

}  // namespace rhc
