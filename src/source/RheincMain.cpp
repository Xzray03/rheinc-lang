// ============================================================
// RheincMain — driver produksi sementara (RhMain).
//
// Pipeline penuh per file: lex -> preprocess -> parse -> semantic ->
// RhAST -> RhIR (releaseModule -> adopt -> finalize -> freeze).
//
// Policy entry point (RhCore/ModuleKind.h) diterapkan TERPUSAT di
// sini untuk jalur produksi:
//   .rhc (Executable) : entry TEPAT 1 (0 -> gagal; >1 -> gagal).
//   .rhh (Declaration): entry TEPAT 0; final verify internal RhIR yang
//                       default-nya menuntut entry diampu policy ini —
//                       hanya error MissingEntryFunction murni yang
//                       diterima untuk modul declaration.
//
// Sementara sederhana; driver RhMain resmi akan mengambil alih dengan
// opsi lengkap (linking antar modul, output binary, dsb).
//
// usage: rheinc [--dump] [-opt] [--target <target-id>]
//               [-o <out.txt>] <file.rhc|.rhh> [...]
//   --dump            cetak IR final (stdout, atau ke -o bila ada)
//   -opt              jalankan optimizer (rantai 5 pass sampai
//                     konvergen) sebelum finalize
//   --target <id>     override target ABI (default x86_64-linux-sysv;
//                     target tak dikenal -> error)
//   -o <out.txt>      tulis output --dump ke file (wajib bareng --dump)
// Exit: 0 semua file sukses; 1 ada file gagal; 2 usage/gagal baca.
// ============================================================

#include "RhAST/AstToIrTranslator.h"
#include "RhAST/IrDump.h"
#include "RhBackend/RhBackend.h"
#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/ModuleKind.h"
#include "RhCore/SourceManager.h"
#include "RhOptimizer/DeadCodeElim.h"
#include "RhOptimizer/DeadFunctionElimination.h"
#include "RhOptimizer/DeadStoreElimination.h"
#include "RhOptimizer/Devirtualization.h"
#include "RhOptimizer/EscapeAnalysis.h"
#include "RhOptimizer/ScalarReplacement.h"
#include "RhOptimizer/SimplifyCfg.h"
#include "RhOptimizer/StoreForwarding.h"
#include "RhOptimizer/StaticVariableUnification.h"
#include "RhOptimizer/StructLayout.h"
#include "RhOptimizer/InlineFunctions.h"
#include "RhOptimizer/Licm.h"
#include "RhOptimizer/LoadElimination.h"
#include "RhOptimizer/LoopUnroll.h"
#include "RhIR/FinalIr.h"
#include "RhIR/IrLinker.h"
#include "RhIR/IrManager.h"
#include "RhIR/IrModule.h"
#include "RhOptimizer/ConstantPropagation.h"
#include "RhOptimizer/CopyPropagation.h"
#include "RhOptimizer/Cse.h"
#include "RhOptimizer/OptimizerManager.h"
#include "RhOptimizer/PeepholeConstFold.h"
#include "RhOptimizer/StrengthReduction.h"
#include "RhOptimizer/UnreachableBlockElim.h"
#include "RhIR/IrVerifier.h"
#include "RhLexer/IdentifierTable.h"
#include "RhParser/Parser.h"
#include "RhPreprocessor/Preprocessor.h"
#include "RhSemantic/SemanticAnalyzer.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace rhc;

namespace {

struct DriverOptions {
    bool dump = false;
    bool opt = false;
    bool emitAsm = false;   // RhBackend: FinalIr -> .s -> .o (tanpa link)
    std::optional<std::string> target;
    std::optional<std::string> output;
    std::vector<std::string> inputs;
};

const char* severityName(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Note: return "note";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Error: return "error";
    case DiagnosticSeverity::Fatal: return "fatal";
    }
    return "diagnostic";
}

void printDiagnostics(const DiagnosticEngine& diagnostics) {
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.path << ':' << diagnostic.line << ':'
                  << diagnostic.column << ": "
                  << severityName(diagnostic.severity) << ": "
                  << diagnostic.message << '\n';
    }
}

// Policy tunggal dari RhCore/ModuleKind.h; ekstensi menentukan kind.
ModuleKind moduleKindForPath(const std::string& path) {
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".rhh") == 0) {
        return ModuleKind::Declaration;
    }
    return ModuleKind::Executable;
}

// Jalankan pipeline penuh untuk SATU file. Mengembalikan 0 sukses, 1 gagal.
// Bila dumpStream != nullptr, IR final (bila ada) ditulis ke sana.
int runOneFile(const std::string& path, const DriverOptions& opts,
               std::ostream* dumpStream,
               std::unique_ptr<IrModule>& outModule) {
    SourceManager sourceManager;
    std::string error;
    const FileID file = sourceManager.addFileFromDisk(path, error);
    if (!file.valid()) {
        std::cerr << "rheinc: gagal baca file '" << path << "': " << error
                  << "\n";
        return 2;
    }

    IdentifierTable identifiers;
    LangOptions options;
    DiagnosticEngine diagnostics(sourceManager, options.maxLexicalErrors);
    Preprocessor preprocessor(sourceManager, diagnostics, identifiers, options);
    preprocessor.setMainFile(file);
    Parser parser(preprocessor, sourceManager, diagnostics);
    TranslationUnitDecl* unit = parser.parseTranslationUnit();
    if (diagnostics.hasErrors() || !unit) {
        printDiagnostics(diagnostics);
        return 1;
    }

    SemanticAnalyzer semantic(sourceManager, diagnostics);
    semantic.analyze(unit);
    if (diagnostics.hasErrors()) {
        printDiagnostics(diagnostics);
        return 1;
    }

    AstToIrTranslator translator(sourceManager, diagnostics);
    IrModule* module = translator.translate(unit, semantic);
    if (!module) {
        printDiagnostics(diagnostics);
        return 1;
    }
    module->sourcePath = path;

    auto releasedModule = translator.releaseModule();
    if (!releasedModule) {
        std::cerr << "rheinc: releaseModule() gagal setelah translate sukses\n";
        return 1;
    }

    IrManagerOptions managerOptions;
    if (opts.target.has_value()) {
        managerOptions.explicitTarget = *opts.target;
    }
    IrManager irManager(managerOptions);
    auto adoptReport = irManager.adoptModule(std::move(releasedModule));
    if (!adoptReport.ok) {
        std::cerr << "rheinc: RhIR adopt gagal: " << adoptReport.error.message
                  << "\n";
        return 1;
    }

    if (opts.opt) {
        OptimizerOptions optimizerOptions;
        optimizerOptions.verifyEachPass = true;
        optimizerOptions.iterateToFixpoint = true;
        OptimizerManager optimizer(std::move(optimizerOptions));
        // const-prop dulu: propagasi konstanta ke operand membuka
        // peluang fold; iterasi fixpoint mengulang rantai sampai stabil.
        optimizer.registerPass(std::make_unique<ConstantPropagation>());
        optimizer.registerPass(std::make_unique<PeepholeConstFold>());
        optimizer.registerPass(std::make_unique<StaticVariableUnification>());
        optimizer.registerPass(std::make_unique<StructLayoutOptimize>());
        optimizer.registerPass(std::make_unique<StrengthReduction>());
        optimizer.registerPass(std::make_unique<CopyPropagation>());
        optimizer.registerPass(std::make_unique<CommonSubexpressionElimination>());
        optimizer.registerPass(std::make_unique<Devirtualization>());
        optimizer.registerPass(std::make_unique<EscapeAnalysis>());
        optimizer.registerPass(std::make_unique<ScalarReplacement>());
        optimizer.registerPass(std::make_unique<StoreForwarding>());
        optimizer.registerPass(std::make_unique<DeadStoreElimination>());
        optimizer.registerPass(std::make_unique<LoadElimination>());
        optimizer.registerPass(std::make_unique<SimplifyCfg>());
        // LICM diaktifkan kembali: dua "bug" yang dulu memicu penonaktifan
        // ((A) dominance violation, (B) segfault) terbukti artefak binary
        // stale saat build paralel — corpus build penuh selalu lulus 76/10
        // x 3 mode dengan LICM terdaftar. Debug: RHEINC_DEBUG_LICM=1.
        optimizer.registerPass(std::make_unique<LoopInvariantCodeMotion>());
        optimizer.registerPass(std::make_unique<LoopUnroll>());
        optimizer.registerPass(std::make_unique<DeadCodeElim>());
        optimizer.registerPass(std::make_unique<DeadFunctionElimination>());
        optimizer.registerPass(std::make_unique<InlineFunctions>());
        optimizer.registerPass(std::make_unique<UnreachableBlockElim>());
        OptimizerReport optimizerReport = optimizer.runAll(irManager.module());
        if (!optimizerReport.ok) {
            std::cerr << "rheinc: optimizer gagal: "
                      << optimizerReport.errorMessage << "\n";
            return 1;
        }
        if (dumpStream != nullptr) {
            for (const auto& step : optimizerReport.results) {
                *dumpStream << "; pass " << step.first
                            << " changed=" << step.second.changed
                            << " n=" << step.second.folded << "\n";
            }
        }
    }

    const ModuleKind moduleKind = moduleKindForPath(path);
    auto finalizeReport = irManager.finalize();
    if (!finalizeReport.ok) {
        // Policy RhCore: modul declaration (.rhh) diberi keringanan HANYA
        // untuk MissingEntryFunction murni. Error lain tetap fatal.
        const auto& verifyErrors = finalizeReport.verifyReport.errors;
        const bool onlyMissingEntry =
            !verifyErrors.empty() &&
            std::all_of(verifyErrors.begin(), verifyErrors.end(),
                        [](const IrVerifyError& e) {
                            return e.code ==
                                   IrVerifyErrorCode::MissingEntryFunction;
                        });
        if (moduleKind == ModuleKind::Declaration && onlyMissingEntry) {
            outModule = std::make_unique<IrModule>(irManager.module());
            std::cout << "rheinc: OK (declaration-only, policy RhCore: "
                      << "0 entry diterima) " << path << "\n";
            return 0;  // freeze dilewati: declaration tak punya entry
        }
        std::cerr << "rheinc: RhIR finalize gagal: "
                  << finalizeReport.error.message << "\n";
        for (const auto& verifyError : finalizeReport.verifyReport.errors) {
            std::cerr << "rheinc: verify: " << verifyError.message << "\n";
        }
        for (const auto& layoutError : finalizeReport.layoutReport.errors) {
            std::cerr << "rheinc: layout: " << layoutError.message << "\n";
        }
        return 1;
    }

    FinalIr finalIr = irManager.freeze();
    if (!finalIr) {
        std::cerr << "rheinc: RhIR freeze gagal menghasilkan FinalIr\n";
        return 1;
    }

    if (moduleKind == ModuleKind::Executable &&
        finalIr.entryFunction() == InvalidFunc) {
        std::cerr << "rheinc: policy RhCore dilanggar: executable tanpa "
                  << "entry padahal finalize lolos\n";
        return 1;
    }

    if (opts.emitAsm && moduleKind == ModuleKind::Executable) {
        // RhBackend Fase 0: <input>.s -> <input>.o (link ditunda RhLinker)
        std::string asmPath = path;
        const std::string::size_type dot = asmPath.rfind('.');
        if (dot != std::string::npos) {
            asmPath.resize(dot);
        }
        asmPath += ".s";
        RhBackendOptions backendOptions;
        backendOptions.outputPath = asmPath;
        const RhBackendReport backendReport =
            RhBackend{}.emitAssembly(finalIr, backendOptions);
        if (!backendReport.ok) {
            for (const RhBackendError& err : backendReport.errors) {
                std::cerr << "rheinc: backend: ";
                if (!err.functionName.empty()) {
                    std::cerr << err.functionName << ": ";
                }
                std::cerr << err.message << "\n";
            }
            return 1;
        }
        std::cout << "rheinc: asm " << asmPath << "\n";
    }

    if (dumpStream != nullptr) {
        IrDumpOptions dumpOptions;
        dumpIrModule(*dumpStream, finalIr.module(), dumpOptions);
    }
    outModule = std::make_unique<IrModule>(finalIr.module());
    std::cout << "rheinc: OK " << path << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    DriverOptions opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dump") {
            opts.dump = true;
        } else if (arg == "--emit-asm") {
            opts.emitAsm = true;
        } else if (arg == "-opt") {
            opts.opt = true;
        } else if (arg == "--target") {
            if (++i >= argc) {
                std::cerr << "rheinc: --target butuh nilai\n";
                return 2;
            }
            opts.target = argv[i];
        } else if (arg == "-o") {
            if (++i >= argc) {
                std::cerr << "rheinc: -o butuh nama file\n";
                return 2;
            }
            opts.output = argv[i];
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "usage: rheinc [--dump] [-opt] "
                      << "[--target <id>] [-o <out.txt>] "
                      << "<file.rhc|.rhh> [...]\n";
            return 2;
        } else {
            opts.inputs.push_back(arg);
        }
    }
    if (opts.inputs.empty() ||
        (opts.output.has_value() && !opts.dump)) {
        std::cerr << "usage: rheinc [--dump] [-opt] "
                  << "[--target <id>] [-o <out.txt>] "
                  << "<file.rhc|.rhh> [...]\n";
        std::cerr << "rheinc: -o hanya valid bersama --dump\n";
        return 2;
    }

    std::ofstream outFile;
    std::ostream* dumpStream = nullptr;
    if (opts.dump) {
        if (opts.output.has_value()) {
            outFile.open(*opts.output);
            if (!outFile) {
                std::cerr << "rheinc: gagal membuka output '"
                          << *opts.output << "'\n";
                return 2;
            }
            dumpStream = &outFile;
        } else {
            dumpStream = &std::cout;
        }
    }

    int exitCode = 0;
    std::vector<std::unique_ptr<IrModule>> linkedModules;
    for (const std::string& path : opts.inputs) {
        std::unique_ptr<IrModule> moduleOut;
        const int rc = runOneFile(path, opts, dumpStream, moduleOut);
        if (rc != 0) {
            exitCode = 1;  // lanjutkan file lain, laporkan gagal
        } else {
            linkedModules.push_back(std::move(moduleOut));
        }
    }

    // Link-time: verifikasi simbol antar modul yang berhasil dikompilasi.
    std::vector<const IrModule*> modulePtrs;
    modulePtrs.reserve(linkedModules.size());
    for (const auto& m : linkedModules) modulePtrs.push_back(m.get());
    const IrLinkReport linkReport = IrLinkVerifier{}.verify(modulePtrs);
    for (const IrLinkError& linkError : linkReport.errors) {
        std::cerr << "rheinc: link: " << linkError.moduleName << ": "
                  << linkError.message << "\n";
    }
    if (!linkReport.ok) exitCode = 1;

    return exitCode;
}
