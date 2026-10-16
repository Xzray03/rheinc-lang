#include "AstToIrTranslatorImpl.h"

#include "RhParser/Decl.h"

namespace rhc {

// Fase 2 entry point: bangun IrModule dari TranslationUnitDecl.
// Dua pass sederhana:
//   1) daftarkan semua fungsi dulu (forward reference antar fungsi),
//   2) lowering isi tiap fungsi.
// Struct/global dikerjakan di fase agregat; yang belum didukung
// diberi pesan unsupported yang jujur.
bool AstToIrTranslator::Impl::lowerTranslationUnit() {
    // Pass 1: pra-daftar fungsi (stub) agar resolveCallee bekerja untuk
    // panggilan forward (fungsi dipakai sebelum didefinisikan).
    for (const DeclNode* decl : unit->declarations) {
        if (decl->kind == AstKind::FunctionDecl ||
            decl->kind == AstKind::ProcedureDecl) {
            const auto* function = static_cast<const FunctionDecl*>(decl);
            const auto found = functionsByName.find(function->name);
            if (found != functionsByName.end()) {
                error(AstIrErrorCode::MalformedShape, decl,
                      "duplikasi deklarasi fungsi '" + function->name + "'");
                return false;
            }
            const FuncId stub = builder.createFunction(
                function->name, InvalidType, function->range.begin);
            module.globals[function->name] = stub;
            functionsByName[function->name] = stub;
        }
    }

    // Pass 2: lowering isi fungsi (stub pass 1 diisi signature & body).
    for (const DeclNode* decl : unit->declarations) {
        switch (decl->kind) {
        case AstKind::FunctionDecl:
        case AstKind::ProcedureDecl:
            if (!lowerFunction(static_cast<const FunctionDecl*>(decl))) {
                return false;
            }
            break;
        case AstKind::StructDecl:
            // Tipe struct dipetakan saat dipakai (lazy) atau saat fase
            // agregat; deklarasi saja tidak menghasilkan instruksi.
            break;
        case AstKind::GlobalVarDecl:
            if (!error(AstIrErrorCode::UnsupportedNode, decl,
                       "variabel global: lowering di fase agregat")) {
                return false;
            }
            break;
        default:
            if (!error(AstIrErrorCode::UnsupportedNode, decl,
                       "deklarasi top-level ini tidak lolos validasi "
                       "fase 1")) {
                return false;
            }
        }
    }
    return !builder.broken();
}

} // namespace rhc
