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
        const FunctionDecl* function = nullptr;
        if (decl->kind == AstKind::FunctionDecl ||
            decl->kind == AstKind::ProcedureDecl) {
            function = static_cast<const FunctionDecl*>(decl);
        } else if (decl->kind == AstKind::GenericDecl) {
            // Fungsi generik didaftarkan juga (stub) agar pemanggilan
            // generik teresolve; instantiasi monomorph penuh adalah
            // tanggung jawab pass berikutnya.
            const auto* generic = static_cast<const GenericDecl*>(decl);
            if (generic->declaration &&
                (generic->declaration->kind == AstKind::FunctionDecl ||
                 generic->declaration->kind == AstKind::ProcedureDecl)) {
                function = static_cast<const FunctionDecl*>(generic->declaration);
            }
        }
        if (!function) continue;
        // Pola prototipe + definisi legal: deklarasi pertama yang
        // memegang FuncId (stub); deklarasi berikutnya dengan nama
        // sama di-skip — lowerFunction me-reuse stub yang sama.
        if (functionsByName.count(function->name)) continue;
        const FuncId stub = builder.createFunction(
            function->name, InvalidType, function->range.begin);
        module.globals[function->name] = stub;
        functionsByName[function->name] = stub;
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
        case AstKind::EnumDecl:
            // Tipe struct/sum dipetakan saat dipakai (lazy); deklarasi
            // saja tidak menghasilkan instruksi.
            break;
        case AstKind::GenericDecl: {
            const auto* generic = static_cast<const GenericDecl*>(decl);
            const DeclNode* inner = generic->declaration;
            if (inner->kind == AstKind::FunctionDecl ||
                inner->kind == AstKind::ProcedureDecl) {
                // Fungsi generik: body dilowering dengan parameter
                // terkonkretisasi default (D-2) — monomorph penuh
                // (per-instantiasi) adalah pekerjaan pass khusus.
                if (!lowerFunction(static_cast<const FunctionDecl*>(inner))) {
                    return false;
                }
            } else if (inner->kind == AstKind::StructDecl ||
                       inner->kind == AstKind::EnumDecl) {
                break;  // lazy, seperti non-generik
            } else {
                if (!error(AstIrErrorCode::UnsupportedNode, decl,
                           "GenericDecl berisi deklarasi yang belum "
                           "didukung")) {
                    return false;
                }
            }
            break;
        }
        case AstKind::GlobalVarDecl: {
            const auto* global = static_cast<const GlobalVarDecl*>(decl);
            if (global->isConst) {
                // Konstanta global (tetap): by-value intern per modul.
                if (!lowerGlobalConst(global)) return false;
                break;
            }
            if (!lowerGlobalMutable(global)) return false;
            break;
        }
        default:
            if (!error(AstIrErrorCode::UnsupportedNode, decl,
                       "deklarasi top-level ini tidak lolos validasi "
                       "fase 1")) {
                return false;
            }
        }
    }

    // Fungsi implisit .__global_init (bila ada global mutable ber-initializer).
    if (!lowerGlobalInitFunction()) return false;
    return !builder.broken();
}

} // namespace rhc
