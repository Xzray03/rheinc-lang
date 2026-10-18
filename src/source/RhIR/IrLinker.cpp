#include "RhIR/IrLinker.h"

#include "RhCore/ModuleKind.h"
#include "RhIR/IrModule.h"

#include <unordered_map>

namespace rhc {

IrLinkReport IrLinkVerifier::verify(
    const std::vector<const IrModule*>& modules) const {
    return verify(modules, {});
}

IrLinkReport IrLinkVerifier::verify(
    const std::vector<const IrModule*>& modules,
    const std::unordered_set<std::string>& externalDefinedSymbols) const {
    IrLinkReport report;
    report.ok = true;

    const auto isDefinition = [](IrFunctionKind kind) {
        // GlobalInit (.__global_init per modul) sengaja DIKECUALIKAN:
        // global init bersifat per-modul dan digabung backend lewat
        // urutan eksekusi, bukan lewat nama simbol.
        return kind == IrFunctionKind::Normal ||
               kind == IrFunctionKind::Entry;
    };

    // Definisi pertama per nama (untuk deteksi duplikat & resolusi).
    struct DefSite {
        std::string moduleName;
    };
    std::unordered_map<std::string, DefSite> definitions;

    for (const IrModule* module : modules) {
        if (module == nullptr) continue;
        for (const IrFunction& fn : module->functions) {
            if (!isDefinition(fn.kind)) continue;
            const auto [it, inserted] = definitions.try_emplace(
                fn.name, DefSite{module->sourcePath});
            if (!inserted) {
                report.ok = false;
                report.errors.push_back(
                    {IrLinkErrorCode::DuplicateDefinition,
                     module->sourcePath, fn.name,
                     "definisi duplikat '" + fn.name + "': sudah didefinisikan di '" +
                         it->second.moduleName + "'"});
            }
        }
    }

    // Eksternal tak terdefinisi — hanya untuk modul executable.
    for (const IrModule* module : modules) {
        if (module == nullptr) continue;
        ModuleKind kind = ModuleKind::Executable;
        if (module->sourcePath.size() >= 4 &&
            module->sourcePath.compare(module->sourcePath.size() - 4, 4,
                                       ".rhh") == 0) {
            kind = ModuleKind::Declaration;
        }
        if (kind != ModuleKind::Executable) continue;  // .rhh bebas
        for (const IrFunction& fn : module->functions) {
            if (fn.kind != IrFunctionKind::External) continue;
            if (fn.isBuiltin) continue;  // di-link ke runtime bahasa
            if (definitions.count(fn.name) == 0 &&
                externalDefinedSymbols.count(fn.name) == 0) {
                report.ok = false;
                report.errors.push_back(
                    {IrLinkErrorCode::UnresolvedExternal,
                     module->sourcePath, fn.name,
                     "eksternal tak terdefinisi '" + fn.name +
                         "' (tidak ada definisi di modul manapun)"});
            }
        }
    }

    return report;
}

} // namespace rhc
