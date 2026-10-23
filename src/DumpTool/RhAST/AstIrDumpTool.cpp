#include "RhAST/AstToIrTranslator.h"
#include "RhIR/IrManager.h"
#include "RhAST/IrDump.h"
#include "RhCore/DiagnosticEngine.h"
#include "RhCore/ModuleKind.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhParser/Parser.h"
#include "RhPreprocessor/Preprocessor.h"
#include "RhSemantic/SemanticAnalyzer.h"

#include <algorithm>
#include <iostream>
#include <string>

using namespace rhc;

namespace {

const char* severityName(DiagnosticSeverity severity) {
    switch (severity) {
    case DiagnosticSeverity::Note: return "note";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Error: return "error";
    case DiagnosticSeverity::Fatal: return "fatal";
    }
    return "diagnostic";
}

// Spec Bagian 9.1: diagnostic -> stderr.
void printDiagnostics(const DiagnosticEngine& diagnostics) {
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.path << ':' << diagnostic.line << ':'
                  << diagnostic.column << ": "
                  << severityName(diagnostic.severity) << ": "
                  << diagnostic.message << '\n';
    }
}

// Policy entry point (RhCore/ModuleKind.h) diterapkan di LEVEL PROGRAM
// DUMP ini, bukan di library modul. RhMain (belum ada) nanti yang
// mengambil alih penerapan policy secara terpusat.
ModuleKind moduleKindForPath(const std::string& path) {
    if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".rhh") == 0) {
        return ModuleKind::Declaration;
    }
    return ModuleKind::Executable;
}

} // namespace

// rheinc_ast_test [--full] [--no-loc] <file.rhc|.rhh>
//
// Pipeline penuh: lex -> preprocess -> parse -> semantic (wajib ok) ->
// RhAST -> RhIR (releaseModule -> adopt -> finalize -> freeze) ->
// dump IR final ke stdout.
// Exit: 0 sukses; 1 error (stage manapun); 2 usage/gagal baca file.
int main(int argc, char** argv) {
    IrDumpOptions dumpOptions;
    std::string path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--full") {
            dumpOptions.full = true;
        } else if (arg == "--no-loc") {
            dumpOptions.noLoc = true;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "usage: rheinc_ast_test [--full] [--no-loc] <file.rhc|.rhh>\n";
            return 2;
        } else {
            path = arg;
        }
    }
    if (path.empty()) {
        std::cerr << "usage: rheinc_ast_test [--full] [--no-loc] <file.rhc|.rhh>\n";
        return 2;
    }

    SourceManager sourceManager;
    std::string error;
    const FileID file = sourceManager.addFileFromDisk(path, error);
    if (!file.valid()) {
        std::cerr << "rheinc_ast_test: gagal baca file '" << path
                  << "': " << error << "\n";
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
        return 1;  // diagnostic Error/Fatal sudah masuk engine
    }

    module->sourcePath = path;

    // Bridge RhAST -> RhIR permanen: releaseModule -> adopt -> finalize
    // -> freeze SELALU dijalankan (env RHEINC_AST_FINALIZE legacy sudah
    // tidak dipakai lagi sejak integrasi permanen). Policy entry point
    // diterapkan per program dump; RhMain mengambil alih untuk jalur
    // produksi.
    auto releasedModule = translator.releaseModule();
    if (!releasedModule) {
        std::cerr << "rheinc_ast_test: releaseModule() gagal setelah translate sukses\n";
        return 1;
    }

    IrManager irManager;
    auto adoptReport = irManager.adoptModule(std::move(releasedModule));
    if (!adoptReport.ok) {
        std::cerr << "rheinc_ast_test: RhIR adopt gagal: " << adoptReport.error.message << "\n";
        for (const auto& verifyError : adoptReport.verifyReport.errors) {
            std::cerr << "RhIR adopt: " << verifyError.message << "\n";
        }
        return 1;
    }
    // Policy entry point RhCore diterapkan di level tool: .rhc wajib
    // entry TEPAT 1, .rhh (declaration-only) entry TEPAT 0.
    const ModuleKind moduleKind = moduleKindForPath(path);
    // TEMP-DEBUG: cetak type table sebelum finalize.
    if (std::getenv("RHEINC_DEBUG_TYPES") != nullptr) {
        const IrModule& m = irManager.module();
        for (std::size_t i = 0; i < m.types.size(); ++i) {
            std::cerr << "TYPE id=" << i
                      << " kind=" << static_cast<int>(m.types[i].kind)
                      << " sizeBytes=" << m.types[i].sizeBytes
                      << " alignBytes=" << m.types[i].alignBytes
                      << " signed=" << m.types[i].isSigned
                      << " name='" << m.types[i].name << "'\n";
        }
    }
    auto finalizeReport = irManager.finalize();
    if (!finalizeReport.ok) {
        // Modul declaration-only: Final Verify internal RhIR selalu
        // menuntut entry (default executable). Policy RhCore mengizinkan
        // tepat 0 entry untuk Declaration, jadi tool menerima kegagalan
        // HANYA jika SELURUH error-nya MissingEntryFunction — error lain
        // (layout, type, dsb.) tetap gagal. Freeze dilewati karena modul
        // declaration tidak punya entry untuk ditanyakan FinalIr.
        const auto& verifyErrors = finalizeReport.verifyReport.errors;
        const bool onlyMissingEntry =
            !verifyErrors.empty() &&
            std::all_of(verifyErrors.begin(), verifyErrors.end(),
                        [](const IrVerifyError& e) {
                            return e.code ==
                                   IrVerifyErrorCode::MissingEntryFunction;
                        });
        if (moduleKind == ModuleKind::Declaration && onlyMissingEntry) {
            std::cout << "; module declaration-only '"
                      << moduleKindName(ModuleKind::Declaration)
                      << "': final verify internal menuntut entry, "
                      << "diampu policy RhCore (0 entry diterima)\n";
            return 0;
        }
        std::cerr << "rheinc_ast_test: RhIR finalize gagal: " << finalizeReport.error.message << "\n";
        for (const auto& verifyError : finalizeReport.verifyReport.errors) {
            std::cerr << "RhIR verify: " << verifyError.message << "\n";
        }
        for (const auto& layoutError : finalizeReport.layoutReport.errors) {
            std::cerr << "RhIR layout: " << layoutError.message << "\n";
        }
        return 1;
    }
    FinalIr finalIr = irManager.freeze();
    if (!finalIr) {
        std::cerr << "rheinc_ast_test: RhIR freeze gagal menghasilkan FinalIr\n";
        return 1;
    }

    // Assertion policy untuk modul executable: FinalIr yang lolos Final
    // Verify harus punya entry (tepat 1; duplikat sudah ditolak RhIR).
    if (moduleKind == ModuleKind::Executable &&
        finalIr.entryFunction() == InvalidFunc) {
        std::cerr << "rheinc_ast_test: policy RhCore dilanggar: modul "
                  << "executable tanpa entry padahal finalize lolos\n";
        return 1;
    }

    dumpIrModule(std::cout, finalIr.module(), dumpOptions);
    return 0;
}
