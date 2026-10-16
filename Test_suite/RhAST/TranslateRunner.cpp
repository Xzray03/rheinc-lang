
#include "RhAST/AstToIrTranslator.h"
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

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string path = argv[i];
        SourceManager sm;
        std::string error;
        const FileID file = sm.addFileFromDisk(path, error);
        if (!file.valid()) { std::cout << "SKIP " << path << "\n"; continue; }
        IdentifierTable identifiers;
        LangOptions options;
        DiagnosticEngine diagnostics(sm, options.maxLexicalErrors);
        Preprocessor preprocessor(sm, diagnostics, identifiers, options);
        preprocessor.setMainFile(file);
        Parser parser(preprocessor, sm, diagnostics);
        TranslationUnitDecl* unit = parser.parseTranslationUnit();
        SemanticAnalyzer semantic(sm, diagnostics);
        semantic.analyze(unit);
        if (diagnostics.hasErrors()) {
            std::cout << "SEMANTIC-FAIL " << path << ": "
                      << diagnostics.diagnostics().back().message << "\n";
            continue;
        }
        AstToIrTranslator translator(sm, diagnostics, AstToIrOptions{.verifyAfterEachMutation = true});
        IrModule* module = translator.translate(unit, semantic);
        if (!module) {
            std::cout << "TRANSLATE-FAIL " << path << ": "
                      << diagnostics.diagnostics().back().message << "\n";
        } else {
            std::cout << "OK " << path << " (fn=" << module->functions.size()
                      << " const=" << module->constants.size() << ")\n";
        }
    }
    return 0;
}
