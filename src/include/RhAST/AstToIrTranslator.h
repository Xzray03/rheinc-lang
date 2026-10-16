#pragma once

#include "RhAST/AstToIrOptions.h"
#include "RhAST/IrDump.h"
#include "RhAST/IrModule.h"

#include <iosfwd>
#include <memory>
#include <string>

namespace rhc {

class DiagnosticEngine;
class SemanticAnalyzer;
class SourceManager;
struct TranslationUnitDecl;

// Translator AST -> IR SSA (spesifikasi Bagian 7).
//
// Read-only translator dengan validasi: menerima AST in-memory hasil
// RhParser yang telah dianalisis RhSemantic, menghasilkan IrModule.
// RhAST BUKAN IR — hasil kerjanya adalah objek IrModule.
//
// Prasyarat keras: translate() tidak pernah memproses apa pun jika
// SemanticAnalyzer menghasilkan satu pun Error/Fatal.
//
// Dua fase (spesifikasi Bagian 7.4):
//   Fase 1 — validasi AST (walk read-only; hard-stop error pertama)
//   Fase 2 — lowering      (bangun IR;     hard-stop error pertama)
//
// Ownership (spesifikasi Bagian 7.3): translator MEMILIKI IrModule dan
// arena penyimpanan node IR. Caller wajib menjaga translator tetap hidup
// selama IR dipakai; hasil translate() adalah pointer non-owning.
class AstToIrTranslator {
public:
    AstToIrTranslator(SourceManager& sourceManager,
                      DiagnosticEngine& diagnostics,
                      AstToIrOptions options = {});
    ~AstToIrTranslator();

    AstToIrTranslator(const AstToIrTranslator&) = delete;
    AstToIrTranslator& operator=(const AstToIrTranslator&) = delete;

    // Gagal -> nullptr; diagnostic (Error/Fatal) sudah masuk
    // DiagnosticEngine. Panggilan kedua setelah kegagalan dilarang.
    IrModule* translate(TranslationUnitDecl* unit, SemanticAnalyzer& semantic);

    // Valid hanya setelah translate() sukses.
    IrModule& module();
    const IrModule& module() const;

    const TypeMapping& typeMapping() const;
    const SymbolMapping& symbolMapping() const;

    void dump(std::ostream& out, const IrDumpOptions& options = {}) const;

public:
    // Opaque type — definisi lengkap hanya di internal header
    // source/RhAST/AstToIrTranslatorImpl.h (konvensi satu file = satu
    // fungsi mewajibkan definisi method Impl tersebar di banyak .cpp).
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace rhc
