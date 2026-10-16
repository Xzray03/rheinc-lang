#include "RhAST/AstToIrTranslator.h"
#include "RhAST/IrDump.h"
#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhParser/Parser.h"
#include "RhPreprocessor/Preprocessor.h"
#include "RhSemantic/SemanticAnalyzer.h"

#include <iostream>
#include <string>

using namespace rhc;

// rheinc_ast_test [--full] [--no-loc] <file.rhc>
//
// Pipeline: lex -> preprocess -> parse -> semantic(wajib ok) ->
// RhAST(fase1+fase2) -> dump IR text ke stdout.
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
            std::cerr << "usage: rheinc_ast_test [--full] [--no-loc] <file.rhc>\n";
            return 2;
        } else {
            path = arg;
        }
    }
    if (path.empty()) {
        std::cerr << "usage: rheinc_ast_test [--full] [--no-loc] <file.rhc>\n";
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
        return 1;
    }

    SemanticAnalyzer semantic(sourceManager, diagnostics);
    semantic.analyze(unit);
    if (diagnostics.hasErrors()) {
        return 1;
    }

    AstToIrTranslator translator(sourceManager, diagnostics);
    IrModule* module = translator.translate(unit, semantic);
    if (!module) {
        return 1;  // diagnostic Error/Fatal sudah masuk engine
    }

    module->sourcePath = path;
    translator.dump(std::cout, dumpOptions);
    return 0;
}
