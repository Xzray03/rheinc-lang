
#include "RhAST/ValidateAst.h"
#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhParser/Parser.h"
#include "RhPreprocessor/Preprocessor.h"

#include <cassert>
#include <iostream>

static struct FlushInit { FlushInit() { std::cout.setf(std::ios::unitbuf); } } flushInit;
#include <string>

using namespace rhc;

// Parse file, jalankan ValidateAst, kembalikan (ok, pesan diagnostic terakhir).
static std::pair<bool, std::string> run(const std::string& path) {
    SourceManager sm;
    std::string error;
    const FileID file = sm.addFileFromDisk(path, error);
    assert(file.valid());

    IdentifierTable identifiers;
    LangOptions options;
    DiagnosticEngine diagnostics(sm, options.maxLexicalErrors);
    Preprocessor preprocessor(sm, diagnostics, identifiers, options);
    preprocessor.setMainFile(file);
    Parser parser(preprocessor, sm, diagnostics);
    TranslationUnitDecl* unit = parser.parseTranslationUnit();
    assert(unit != nullptr);

    DiagnosticEngine validateDiag(sm);
    const bool ok = ValidateAst(*unit, validateDiag);
    std::string msg = validateDiag.hasErrors()
        ? validateDiag.diagnostics().back().message
        : "";
    return {ok, msg};
}

int main(int /*argc*/, char** argv) {
    const std::string root = argv[1];
    int failures = 0;

    // Tier 1: harus lulus validasi fase 1
    const char* clean[] = {
        "halo_dunia.rhc", "integer.rhc", "teks.rhc", "percabangan.rhc",
        "perulangan.rhc", "fungsi_dan_prosedur.rhc", "struktur.rhc",
    };
    for (const char* name : clean) {
        auto [ok, msg] = run(root + "/sintaks-awal/" + name);
        if (!ok) {
            std::cout << "GAGAL [clean] " << name << ": " << msg << "\n";
            ++failures;
        } else {
            std::cout << "OK   [clean] " << name << "\n";
        }
    }

    // Fitur belum didukung: harus ditolak ast.unsupported_node.
    // defer.rhc ikut di sarena memakai pilih (skenario 7) — akan pindah ke
    // daftar clean setelah SwitchTag/SwitchInt di-lowering.
    const char* unsupported[] = {
        "lambda.rhc", "pilih.rhc", "generator.rhc", "defer.rhc",
    };
    for (const char* name : unsupported) {
        auto [ok, msg] = run(root + "/sintaks-awal/" + name);
        if (ok || msg.find("ast.unsupported_node") == std::string::npos) {
            std::cout << "GAGAL [unsupported] " << name << ": ok=" << ok
                      << " msg=" << msg << "\n";
            ++failures;
        } else {
            std::cout << "OK   [unsupported] " << name << ": " << msg << "\n";
        }
    }

    if (failures) {
        std::cout << failures << " TEST GAGAL\n";
        return 1;
    }
    std::cout << "SEMUA TEST ValidateAst LULUS\n";
    return 0;
}
