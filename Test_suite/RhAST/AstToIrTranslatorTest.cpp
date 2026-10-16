
#include "RhAST/AstToIrTranslator.h"
#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhParser/Parser.h"
#include "RhPreprocessor/Preprocessor.h"
#include "RhSemantic/SemanticAnalyzer.h"

#include <cassert>
#include <iostream>
#include <string>

using namespace rhc;

int main(int /*argc*/, char** argv) {
    const std::string path = argv[1];

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

    SemanticAnalyzer semantic(sm, diagnostics);
    semantic.analyze(unit);
    assert(!diagnostics.hasErrors());

    AstToIrTranslator translator(sm, diagnostics);
    IrModule* module = translator.translate(unit, semantic);
    assert(module != nullptr);

    // --- assert struktur halo_dunia ---
    assert(module->irVersion == 1);
    // utama + stub eksternal tampilkan
    assert(module->functions.size() == 2);
    const IrFunction& utama = module->functions[module->globals.at("utama")];
    assert(utama.name == "utama");
    assert(utama.blocks.size() == 1);
    // dua statement tampilkan: ConstString, Call, ConstString, Call, RetVoid
    assert(utama.instrs.size() == 5);
    assert(utama.instrs[0].op == IrOpcode::ConstString);
    assert(utama.instrs[1].op == IrOpcode::Call);
    assert(utama.instrs[1].operands[0].kind == IrOperandKind::FuncV);
    assert(utama.instrs[1].operands[1].kind == IrOperandKind::ValueV);
    assert(utama.instrs[2].op == IrOpcode::ConstString);
    assert(utama.instrs[3].op == IrOpcode::Call);
    assert(utama.instrs[4].op == IrOpcode::RetVoid);
    // constant pool: dua string ter-decode benar
    assert(module->constants.size() == 2);
    assert(module->constants[0].kind == IrConstantKind::String);
    assert(module->constants[0].length == 13);
    assert(module->constants[1].length == 26);
    // stub eksternal terdaftar
    assert(module->globals.count("tampilkan") == 1);
    assert(module->globals.count("utama") == 1);
    // tipe: View(teks) + byte-elemen + void + fnptr utama
    assert(module->types.size() == 4);

    std::cout << "TEST halo_dunia LULUS\n";
    return 0;
}
