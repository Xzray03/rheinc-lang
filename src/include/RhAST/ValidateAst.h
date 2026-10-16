#pragma once

#include "RhCore/DiagnosticEngine.h"

namespace rhc {

struct TranslationUnitDecl;

// Fase 1 RhAST — validasi AST secara read-only (spesifikasi Bagian 7.4).
//
// Walker menolak:
//   - node placeholder hasil recovery parser     -> ast.error_node
//   - node valid yang belum dapat di-lowering    -> ast.unsupported_node
//   - struktur AST tidak sah (child null wajib)  -> ast.malformed_shape
//
// Hard-stop pada error pertama: fungsi mengembalikan false tepat setelah
// error pertama dilaporkan ke DiagnosticEngine; sisa tree tidak diperiksa.
// AST tidak pernah dimutasi — validasi murni pembacaan.
bool ValidateAst(const TranslationUnitDecl& unit, DiagnosticEngine& diagnostics);

} // namespace rhc
