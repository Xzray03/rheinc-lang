#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhParser/Parser.h"
#include "RhPreprocessor/Preprocessor.h"
#include "RhSemantic/SemanticAnalyzer.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: rheinc-semantic-dump <file.rhc>\n";
        return 2;
    }

    rhc::SourceManager sourceManager;
    std::string error;
    const rhc::FileID file = sourceManager.addFileFromDisk(argv[1], error);
    if (!file.valid()) {
        std::cerr << error << '\n';
        return 2;
    }

    rhc::IdentifierTable identifiers;
    rhc::LangOptions options;
    rhc::DiagnosticEngine diagnostics(sourceManager, options.maxLexicalErrors);
    rhc::Preprocessor preprocessor(sourceManager, diagnostics, identifiers, options);
    preprocessor.setMainFile(file);
    rhc::Parser parser(preprocessor, sourceManager, diagnostics);
    rhc::TranslationUnitDecl* unit = parser.parseTranslationUnit();

    rhc::SemanticAnalyzer semantic(sourceManager, diagnostics);
    const bool semaOk = semantic.analyze(unit);

    std::cout << "(semantic " << (semaOk ? "ok" : "error") << ")\n";

    for (const rhc::Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.path << ':' << diagnostic.line << ':' << diagnostic.column
                  << ": error: " << diagnostic.message << '\n';
    }

    return diagnostics.hasErrors() ? 1 : 0;
}
