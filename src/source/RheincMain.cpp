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
// usage: rheinc <main.rhc> [-o|-oo|-os|-od|-dump] ...
//               [-o <out.txt>] <file.rhc|.rhh> [...]
//   -dump             cetak IR final ke stdout
//   -opt              jalankan optimizer (rantai 5 pass sampai
//                     konvergen) sebelum finalize
//   --target <id>     override target ABI (default x86_64-linux-sysv;
//                     target tak dikenal -> error)
//   -o <out.exe>      build Executable
// Exit: 0 semua file sukses; 1 ada file gagal; 2 usage/gagal baca.
// ============================================================

#include "RhAST/AstToIrTranslator.h"
#include "RhAST/IrDump.h"
#include "RhBackend/RhBackend.h"
#include "RhLinker/RhLinker.h"
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
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace rhc;

namespace {

enum class BuildTarget {
    Executable,
    Object,
    StaticLibrary,
    DynamicLibrary,
};

// Status yang dikirim RhMain untuk setiap file yang dikompilasi.
// `isMainFile` true hanya untuk file di argumen paling pertama.
struct DriverOptions {
    bool dump = false;
    bool opt = false;
    bool emitAsm = false;      // RhBackend: FinalIr -> .s -> .o
    bool doLink = false;       // RhLinker: object + library runtime -> exe
    BuildTarget buildTarget = BuildTarget::Executable;
    bool targetSet = false;
    std::optional<std::string> abiTarget;
    std::optional<std::string> output;
    std::string mainInput;
    std::vector<std::string> linkInputs;
    std::vector<std::string> importDirs;
    bool linkSeen = false;
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

// Simbol DEFINED global dari object prebuilt (nm -g --defined-only).
bool collectObjectDefinedSymbols(const std::string& path,
                                 std::unordered_set<std::string>& out) {
    const std::string cmd = "nm -g --defined-only " + path + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) return false;
    char buf[512];
    std::string line;
    auto flushLine = [&]() {
        const std::string::size_type pos = line.find_last_not_of(" \t");
        if (pos == std::string::npos) { line.clear(); return; }
        const std::string::size_type start = line.find_last_of(" \t", pos);
        const std::string sym =
            line.substr(start == std::string::npos ? 0 : start + 1,
                        pos - (start == std::string::npos ? 0 : start));
        // Untuk arsip .a, nm mencetak header member seperti `foo.o:`;
        // jangan jadikan itu simbol user.
        if (!sym.empty() && sym.back() != ':' &&
            sym.find("no symbols") == std::string::npos) {
            out.insert(sym);
        }
        line.clear();
    };
    while (std::fgets(buf, sizeof(buf), pipe)) {
        for (const char c : std::string(buf)) {
            if (c == '\n') flushLine();
            else if (c != '\r') line += c;
        }
    }
    flushLine();
    const int rc = pclose(pipe);
    return rc == 0;
}

bool runCommandCapture(const std::string& cmd, std::string* output) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) return false;
    char buf[512];
    if (output != nullptr) {
        while (std::fgets(buf, sizeof(buf), pipe)) *output += buf;
    }
    const int rc = pclose(pipe);
    return rc == 0;
}

std::string trimCopy(std::string s) {
    const std::string::size_type first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const std::string::size_type last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

bool listArchiveMembers(const std::string& archive,
                        std::vector<std::string>& members) {
    std::string out;
    if (!runCommandCapture("ar t " + archive + " 2>&1", &out)) return false;
    std::string line;
    auto flush = [&]() {
        line = trimCopy(line);
        if (!line.empty()) members.push_back(line);
        line.clear();
    };
    for (const char c : out) {
        if (c == '\n') flush();
        else if (c != '\r') line += c;
    }
    flush();
    return true;
}

// Ekstrak member archive ke direktori sementara dan rename unik agar aman
// saat beberapa archive punya member dengan nama sama.
bool extractArchiveMembersForMerge(const std::string& archive,
                                   std::size_t archiveIndex,
                                   std::vector<std::string>& memberPaths,
                                   std::string* tempDirOut) {
    std::string tmp;
    if (!runCommandCapture("mktemp -d /tmp/rheinc_ar_merge.XXXXXX 2>/dev/null",
                           &tmp)) {
        return false;
    }
    tmp = trimCopy(tmp);
    if (tmp.empty()) return false;

    std::string extractOut;
    if (!runCommandCapture("cd " + tmp + " && ar x " + archive + " 2>&1",
                           &extractOut)) {
        runCommandCapture("rm -rf " + tmp + " 2>/dev/null", nullptr);
        return false;
    }

    std::vector<std::string> members;
    if (!listArchiveMembers(archive, members)) {
        runCommandCapture("rm -rf " + tmp + " 2>/dev/null", nullptr);
        return false;
    }

    for (const std::string& member : members) {
        std::string safe = member;
        std::replace(safe.begin(), safe.end(), '/', '_');
        const std::string src = tmp + "/" + member;
        const std::string dst =
            tmp + "/" + std::to_string(archiveIndex) + "_" + safe;
        if (!runCommandCapture("mv " + src + " " + dst + " 2>&1", nullptr)) {
            runCommandCapture("rm -rf " + tmp + " 2>/dev/null", nullptr);
            return false;
        }
        memberPaths.push_back(dst);
    }
    if (tempDirOut != nullptr) *tempDirOut = tmp;
    else runCommandCapture("rm -rf " + tmp + " 2>/dev/null", nullptr);
    return true;
}

// Jalankan pipeline penuh untuk SATU file. Mengembalikan 0 sukses, 1 gagal.
// Bila dumpStream != nullptr, IR final (bila ada) ditulis ke sana.
int runOneFile(const std::string& path, const DriverOptions& opts,
               std::ostream* dumpStream,
               std::unique_ptr<IrModule>& outModule,
               std::vector<std::string>* outObjectPaths,
               bool isMainFile) {
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
    if (opts.abiTarget.has_value()) {
        managerOptions.explicitTarget = *opts.abiTarget;
    }
    managerOptions.verifyOptions.entryPolicy =
        (isMainFile && opts.buildTarget == BuildTarget::Executable)
            ? IrEntryPolicy::ExactlyOne
            : IrEntryPolicy::None;
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

    // Policy entry sudah ditegakkan di Final verify lewat
    // managerOptions.verifyOptions; tidak ada penghapusan/abaikan entry di sini.

    // Emit object untuk target code:
    // - object utama executable (-o): emit file utama + semua file --link
    // - static library (-os): emit file utama + implementasi --link
    // - dynamic library (-od): emit file utama + dependency --link
    // - object tunggal (-oo): emit file utama saja
    if (opts.emitAsm &&
        (isMainFile || opts.buildTarget == BuildTarget::Executable ||
         opts.buildTarget == BuildTarget::StaticLibrary ||
         opts.buildTarget == BuildTarget::DynamicLibrary)) {
        // RhBackend Fase 0: <input>.s -> <input>.o (link ditunda RhLinker).
        // Mode Object menghormati -oo <out.o>: tulis assembly di <out>.s
        // sehingga assembler menghasilkan <out>.o tepat sesuai permintaan.
        std::string asmPath = path;
        if (opts.buildTarget == BuildTarget::Object && opts.output.has_value()) {
            asmPath = *opts.output;
            const std::string::size_type outDot = asmPath.rfind('.');
            if (outDot != std::string::npos &&
                asmPath.compare(outDot, 2, ".o") == 0) {
                asmPath.resize(outDot);
            }
            asmPath += ".s";
        } else {
            const std::string::size_type dot = asmPath.rfind('.');
            if (dot != std::string::npos) {
                asmPath.resize(dot);
            }
            asmPath += ".s";
        }
        RhBackendOptions backendOptions;
        backendOptions.outputPath = asmPath;
        backendOptions.emitCrtGlue =
            isMainFile && opts.buildTarget == BuildTarget::Executable;
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
        if (outObjectPaths != nullptr) {
            std::string objectPath = asmPath;
            const std::string::size_type dot2 = objectPath.rfind('.');
            if (dot2 != std::string::npos) {
                objectPath.resize(dot2);
            }
            objectPath += ".o";
            outObjectPaths->push_back(std::move(objectPath));
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

bool collectObjectUndefinedSymbols(const std::string& path,
                                   std::unordered_set<std::string>& out) {
    const std::string cmd = "nm -u " + path + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) return false;
    char buf[512];
    std::string line;
    auto flushLine = [&]() {
        line = trimCopy(line);
        if (!line.empty() && line.back() != ':' &&
            line.rfind("nm:", 0) != 0 &&
            line.find("no symbols") == std::string::npos) {
            const std::string::size_type start =
                line.find_last_of(" \t");
            const std::string sym =
                line.substr(start == std::string::npos ? 0 : start + 1);
            if (!sym.empty()) out.insert(sym);
        }
        line.clear();
    };
    while (std::fgets(buf, sizeof(buf), pipe)) {
        for (const char c : std::string(buf)) {
            if (c == '\n') flushLine();
            else if (c != '\r') line += c;
        }
    }
    flushLine();
    const int rc = pclose(pipe);
    return rc == 0;
}

void collectBinaryCandidatesRecursive(const std::string& dir,
                                      std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (d == nullptr) return;
    while (struct dirent* ent = readdir(d)) {
        const std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        const std::string full = dir + "/" + name;
        DIR* sub = opendir(full.c_str());
        if (sub != nullptr) {
            closedir(sub);
            collectBinaryCandidatesRecursive(full, out);
            continue;
        }
        const bool isObj = name.size() >= 2 &&
            name.compare(name.size() - 2, 2, ".o") == 0;
        const bool isArchive = name.size() >= 2 &&
            name.compare(name.size() - 2, 2, ".a") == 0;
        const bool isShared = name.size() >= 3 &&
            name.compare(name.size() - 3, 3, ".so") == 0;
        if (isObj || isArchive || isShared) out.push_back(full);
    }
    closedir(d);
}

bool autoResolveImportSymbols(
    const DriverOptions& opts,
    const std::vector<std::unique_ptr<IrModule>>& modules,
    const std::vector<std::string>& explicitObjectPaths,
    std::unordered_set<std::string>& definedSymbols,
    std::vector<std::string>& resolvedPaths) {
    std::unordered_set<std::string> irDefined;
    for (const auto& module : modules) {
        for (const IrFunction& fn : module->functions) {
            if (fn.kind != IrFunctionKind::External) irDefined.insert(fn.name);
        }
    }

    std::set<std::string> needed;
    for (const auto& module : modules) {
        for (const IrFunction& fn : module->functions) {
            if (fn.kind == IrFunctionKind::External &&
                irDefined.count(fn.name) == 0 &&
                definedSymbols.count(fn.name) == 0) {
                needed.insert(fn.name);
            }
        }
    }

    for (const std::string& path : explicitObjectPaths) {
        std::unordered_set<std::string> undef;
        if (!collectObjectUndefinedSymbols(path, undef)) continue;
        for (const std::string& sym : undef) {
            if (sym.rfind("rh_", 0) == 0) continue;  // runtime RhBuiltin
            if (irDefined.count(sym) != 0 ||
                definedSymbols.count(sym) != 0) {
                continue;
            }
            needed.insert(sym);
        }
    }

    std::vector<std::string> candidatePaths;
    for (const std::string& dir : opts.importDirs) {
        collectBinaryCandidatesRecursive(dir, candidatePaths);
    }
    std::sort(candidatePaths.begin(), candidatePaths.end());
    candidatePaths.erase(std::unique(candidatePaths.begin(),
                                     candidatePaths.end()),
                         candidatePaths.end());

    struct Candidate {
        std::string path;
        std::unordered_set<std::string> defined;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(candidatePaths.size());
    for (const std::string& path : candidatePaths) {
        Candidate candidate;
        candidate.path = path;
        if (!collectObjectDefinedSymbols(path, candidate.defined)) continue;
        candidates.push_back(std::move(candidate));
    }

    std::unordered_set<std::string> available = definedSymbols;
    std::set<std::string> missing;
    std::set<std::string> resolvedSet;
    std::vector<std::string> resolvedOrder;
    for (const std::string& sym : needed) {
        if (available.count(sym) != 0) continue;
        bool found = false;
        for (const Candidate& candidate : candidates) {
            if (candidate.defined.count(sym) == 0) continue;
            if (resolvedSet.insert(candidate.path).second) {
                resolvedOrder.push_back(candidate.path);
            }
            for (const std::string& def : candidate.defined) {
                available.insert(def);
            }
            found = true;
            break;
        }
        if (!found) missing.insert(sym);
    }

    // Terapkan hasil parsial walau ada simbol yang hilang, supaya verifier
    // hanya melaporkan simbol yang benar-benar tidak ketemu.
    for (const std::string& path : resolvedOrder) resolvedPaths.push_back(path);
    for (const std::string& sym : available) definedSymbols.insert(sym);

    if (!missing.empty()) {
        std::cerr << "rheinc: simbol tidak ditemukan via -Ilink:\n";
        for (const std::string& sym : missing) {
            std::cerr << "  - " << sym << "\n";
        }
        std::cerr << "rheinc: direktori yang dicari:";
        for (const std::string& dir : opts.importDirs) {
            std::cerr << " " << dir;
        }
        std::cerr << "\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    auto usage = []() {
        std::cerr << "usage: rheinc <main.rhc> [-o <out.exe> | -oo <out.o> | -os <out.a> | -od <out.so> | -dump] "
                  << "[-opt] [--target <id>] [-Ilink <dir>]... [--link <file.rhc/.o/.a/.so> ...]\n"
                  << "  <main.rhc>        file utama; argumen paling pertama\n"
                  << "  -o <out.exe>      build Executable (entry di file utama tepat 1)\n"
                  << "  -oo <out.o>       build Object (tidak boleh ada entry; single file)\n"
                  << "  -os <out.a>       build Static Library (tidak boleh ada entry)\n"
                  << "  -od <out.so>      build Dynamic Library (tidak boleh ada entry)\n"
                  << "  -dump             dump IR final ke stdout\n"
                  << "  -Ilink <dir>      auto-resolve simbol dari .o/.a/.so di direktori (untuk -o/-od)\n"
                  << "  --link <file..>   dependency eksplisit sesuai target output\n";
    };

    DriverOptions opts;
    if (argc < 2 || argv[1][0] == '-') {
        usage();
        return 2;
    }
    opts.mainInput = argv[1];

    bool afterLink = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (afterLink) {
            if (arg.empty() || arg[0] == '-') {
                std::cerr << "rheinc: setelah --link hanya boleh ada file .rhc/.o\n";
                usage();
                return 2;
            }
            opts.linkInputs.push_back(arg);
            continue;
        }

        if (arg == "-o") {
            if (++i >= argc) {
                std::cerr << "rheinc: -o butuh nama file\n";
                return 2;
            }
            opts.buildTarget = BuildTarget::Executable;
            opts.targetSet = true;
            opts.output = argv[i];
            opts.emitAsm = true;
            opts.doLink = true;
        } else if (arg == "-oo") {
            if (++i >= argc) {
                std::cerr << "rheinc: -oo butuh nama file\n";
                return 2;
            }
            opts.buildTarget = BuildTarget::Object;
            opts.targetSet = true;
            opts.output = argv[i];
            opts.emitAsm = true;
            opts.doLink = false;
        } else if (arg == "-os") {
            if (++i >= argc) {
                std::cerr << "rheinc: -os butuh nama file\n";
                return 2;
            }
            opts.buildTarget = BuildTarget::StaticLibrary;
            opts.targetSet = true;
            opts.output = argv[i];
            opts.emitAsm = true;
            opts.doLink = false;
        } else if (arg == "-od") {
            if (++i >= argc) {
                std::cerr << "rheinc: -od butuh nama file\n";
                return 2;
            }
            opts.buildTarget = BuildTarget::DynamicLibrary;
            opts.targetSet = true;
            opts.output = argv[i];
            opts.emitAsm = true;
            opts.doLink = true;
        } else if (arg == "-dump") {
            opts.dump = true;
        } else if (arg == "-opt") {
            opts.opt = true;
        } else if (arg == "-Ilink") {
            if (++i >= argc) {
                std::cerr << "rheinc: -Ilink butuh direktori\n";
                return 2;
            }
            opts.importDirs.push_back(argv[i]);
        } else if (arg == "--link") {
            // --link hanya menandai daftar file implementasi tambahan.
            // Target/output ditentukan eksplisit oleh -o atau -oo.
            opts.linkSeen = true;
            afterLink = true;
        } else if (arg == "--target") {
            if (++i >= argc) {
                std::cerr << "rheinc: --target butuh nilai\n";
                return 2;
            }
            opts.abiTarget = argv[i];
        } else if (arg.empty() || arg[0] == '-') {
            usage();
            return 2;
        } else {
            std::cerr << "rheinc: file tambahan hanya boleh setelah --link\n";
            usage();
            return 2;
        }
    }

    if (!opts.targetSet && !opts.dump) {
        std::cerr << "rheinc: pilih mode: -o <out.exe>, -oo <out.o>, -os <out.a>, -od <out.so>, atau -dump\n";
        usage();
        return 2;
    }
    if (opts.linkSeen && opts.linkInputs.empty()) {
        std::cerr << "rheinc: --link butuh minimal satu dependency\n";
        return 2;
    }
    if (!opts.importDirs.empty() &&
        (!opts.targetSet ||
         (opts.buildTarget != BuildTarget::Executable &&
          opts.buildTarget != BuildTarget::DynamicLibrary))) {
        std::cerr << "rheinc: -Ilink hanya valid untuk target Executable (-o) atau Dynamic Library (-od)\n";
        return 2;
    }
    if (opts.linkSeen &&
        (!opts.targetSet ||
         (opts.buildTarget != BuildTarget::Executable &&
          opts.buildTarget != BuildTarget::StaticLibrary &&
          opts.buildTarget != BuildTarget::DynamicLibrary))) {
        std::cerr << "rheinc: --link hanya valid untuk target Executable (-o), Static Library (-os), atau Dynamic Library (-od)\n";
        return 2;
    }
    if (opts.buildTarget == BuildTarget::StaticLibrary && opts.output.has_value() &&
        (opts.output->size() < 2 ||
         opts.output->compare(opts.output->size() - 2, 2, ".a") != 0)) {
        std::cerr << "rheinc: -os membutuhkan output berakhiran .a\n";
        return 2;
    }
    if (opts.buildTarget == BuildTarget::DynamicLibrary &&
        (!opts.output.has_value() || opts.output->size() < 3 ||
         opts.output->compare(opts.output->size() - 3, 3, ".so") != 0)) {
        std::cerr << "rheinc: -od membutuhkan output berakhiran .so\n";
        return 2;
    }

    std::ofstream outFile;
    std::ostream* dumpStream = nullptr;
    if (opts.dump) {
        dumpStream = &std::cout;
    }

    int exitCode = 0;
    std::vector<std::string> objectPaths;
    std::vector<std::string> archiveLibraryInputs;
    std::unordered_set<std::string> objectDefinedSymbols;
    std::vector<std::unique_ptr<IrModule>> linkedModules;
    std::unique_ptr<IrModule> mainModule;
    const int mainRc = runOneFile(opts.mainInput, opts, dumpStream,
                                  mainModule, &objectPaths,
                                  /*isMainFile=*/true);
    if (mainRc != 0) {
        exitCode = 1;
    } else {
        linkedModules.push_back(std::move(mainModule));
    }

    for (const std::string& path : opts.linkInputs) {
        // Dependency --link campuran:
        //   - target -o : *.rhc/*.o/*.a/*.so
        //   - target -od: *.rhc/*.o/*.a/*.so
        //   - target -os: *.rhc/*.o/*.a saja; *.so dilarang masuk archive
        const bool isObjectFile =
            path.size() >= 2 && path.compare(path.size() - 2, 2, ".o") == 0;
        const bool isArchiveFile =
            path.size() >= 2 && path.compare(path.size() - 2, 2, ".a") == 0;
        const bool isSharedLibraryFile =
            path.size() >= 3 && path.compare(path.size() - 3, 3, ".so") == 0;
        if (isSharedLibraryFile &&
            opts.buildTarget == BuildTarget::StaticLibrary) {
            std::cerr << "rheinc: -os tidak boleh menerima Dynamic Library .so sebagai input: "
                      << path << "\n";
            exitCode = 1;
            continue;
        }
        if (isObjectFile || isArchiveFile || isSharedLibraryFile) {
            std::ifstream binaryFile(path, std::ios::binary);
            if (!binaryFile.good()) {
                std::cerr << "rheinc: dependency binary tak ditemukan: "
                          << path << "\n";
                exitCode = 1;
                continue;
            }
            if (!collectObjectDefinedSymbols(path, objectDefinedSymbols)) {
                std::cerr << "rheinc: gagal scan simbol dependency: " << path << "\n";
                exitCode = 1;
                continue;
            }
            if (isArchiveFile &&
                opts.buildTarget == BuildTarget::StaticLibrary) {
                // Archive input untuk target archive akan di-merge sebagai
                // member, bukan disimpan sebagai archive bertingkat.
                archiveLibraryInputs.push_back(path);
            } else {
                objectPaths.push_back(path);
            }
            std::cout << "rheinc: "
                      << (isSharedLibraryFile ? "library " :
                          (isArchiveFile ? "archive " : "object "))
                      << path << "\n";
            continue;
        }

        std::unique_ptr<IrModule> moduleOut;
        const int rc = runOneFile(path, opts, dumpStream, moduleOut,
                                  &objectPaths, /*isMainFile=*/false);
        if (rc != 0) {
            exitCode = 1;
        } else {
            linkedModules.push_back(std::move(moduleOut));
        }
    }

    // Auto-resolve simbol dari direktori -Ilink (hanya -o/-od). Dipanggil
    // setelah dependency eksplisit, sebelum IrLinkVerifier, supaya simbol
    // dari .o/.a/.so hasil auto-detect ikut memuaskan External di modul IR.
    if (!opts.importDirs.empty()) {
        std::vector<std::string> autoResolvedPaths;
        if (!autoResolveImportSymbols(opts, linkedModules, objectPaths,
                                      objectDefinedSymbols,
                                      autoResolvedPaths)) {
            exitCode = 1;
        } else {
            for (const std::string& path : autoResolvedPaths) {
                objectPaths.push_back(path);
                std::cout << "rheinc: import " << path << "\n";
            }
        }
    }

    // Link-time: verifikasi simbol antar modul yang berhasil dikompilasi.
    std::vector<const IrModule*> modulePtrs;
    modulePtrs.reserve(linkedModules.size());
    for (const auto& m : linkedModules) modulePtrs.push_back(m.get());
    const IrLinkReport rawLinkReport =
        IrLinkVerifier{}.verify(modulePtrs, objectDefinedSymbols);
    // Object/Static/Dynamic library boleh memuat simbol eksternal yang baru
    // resolve saat consumer final melink; UnresolvedExternal tidak fatal.
    IrLinkReport linkReport = rawLinkReport;
    if (opts.buildTarget == BuildTarget::Object ||
        opts.buildTarget == BuildTarget::StaticLibrary ||
        opts.buildTarget == BuildTarget::DynamicLibrary) {
        linkReport.errors.clear();
        for (const IrLinkError& err : rawLinkReport.errors) {
            if (err.code != IrLinkErrorCode::UnresolvedExternal) {
                linkReport.errors.push_back(err);
            }
        }
        linkReport.ok = linkReport.errors.empty();
    }
    for (const IrLinkError& linkError : linkReport.errors) {
        std::cerr << "rheinc: link: " << linkError.moduleName << ": "
                  << linkError.message << "\n";
    }
    if (!linkReport.ok) exitCode = 1;

    // Static library: gabungkan semua object menjadi .a. Entry sudah ditolak
    // saat Final verify untuk target ini.
    if (exitCode == 0 && opts.buildTarget == BuildTarget::StaticLibrary) {
        std::vector<std::string> memberPaths = objectPaths;
        std::vector<std::string> mergeTempDirs;
        bool mergeOk = true;
        for (std::size_t i = 0; i < archiveLibraryInputs.size(); ++i) {
            std::string tmpDir;
            if (!extractArchiveMembersForMerge(archiveLibraryInputs[i], i,
                                               memberPaths, &tmpDir)) {
                std::cerr << "rheinc: gagal merge archive "
                          << archiveLibraryInputs[i] << "\n";
                mergeOk = false;
                break;
            }
            mergeTempDirs.push_back(tmpDir);
        }
        if (!mergeOk) {
            for (const std::string& dir : mergeTempDirs) {
                runCommandCapture("rm -rf " + dir + " 2>/dev/null", nullptr);
            }
            return 1;
        }
        if (memberPaths.empty()) {
            for (const std::string& dir : mergeTempDirs) {
                runCommandCapture("rm -rf " + dir + " 2>/dev/null", nullptr);
            }
            std::cerr << "rheinc: -os tidak punya object untuk di-archive\n";
            return 1;
        }

        std::string arCmd = "ar rcs " + *opts.output;
        for (const std::string& obj : memberPaths) arCmd += " " + obj;
        arCmd += " 2>&1";
        std::string arOut;
        const bool arOk = runCommandCapture(arCmd, &arOut);
        for (const std::string& dir : mergeTempDirs) {
            runCommandCapture("rm -rf " + dir + " 2>/dev/null", nullptr);
        }
        if (!arOk) {
            std::cerr << "rheinc: ar gagal membuat " << *opts.output << "\n"
                      << arOut;
            return 1;
        }
        std::cout << "rheinc: archive " << *opts.output << " ("
                  << memberPaths.size() << " merged objects)\n";
    }

    // Link executable SETELAH semua modul sukses: RhLinker menerima semua
    // object user (main + dependency --link) dan memilih runtime yang dipakai.
    if (exitCode == 0 && opts.doLink) {
        RhLinkerOptions linkOpts;
        linkOpts.inputObjects = objectPaths;
        linkOpts.sharedLibrary =
            opts.buildTarget == BuildTarget::DynamicLibrary;
        linkOpts.libraryDir = RHEINC_LIBRARY_DIR;
        std::string defaultExePath = opts.mainInput;
        const std::string::size_type dot = defaultExePath.rfind('.');
        if (dot != std::string::npos) defaultExePath.resize(dot);
        linkOpts.outputPath = opts.output.has_value() ? *opts.output
                                                      : defaultExePath;
        const RhLinkerReport linkerReport = RhLinker{}.link(linkOpts);
        if (!linkerReport.ok) {
            for (const RhLinkerError& err : linkerReport.errors) {
                std::cerr << "rheinc: linker: " << err.message << "\n";
            }
            return 1;
        }
        std::cout << "rheinc: link " << linkOpts.outputPath << " (runtime: ";
        for (std::size_t i = 0; i < linkerReport.selectedObjects.size(); ++i) {
            if (i) std::cout << ", ";
            const std::string& o = linkerReport.selectedObjects[i];
            std::cout << o.substr(o.find_last_of('/') + 1);
        }
        std::cout << ")\n";
    }

    return exitCode;
}
