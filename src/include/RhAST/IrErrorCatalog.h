#pragma once

#include <cstdint>

namespace rhc {

// Katalog error RhAST (spesifikasi Bagian 10).
// Prinsip: kode error adalah STRING STABIL diawal pesan
// ("error: ast.<kode>: <detail>"). Tidak ada warning — anomali user =
// Error, invariant internal = Fatal (Fatal dijalankan lewat
// DiagnosticEngine, bukan lewat kode di sini).
enum class AstIrErrorCode : std::uint16_t {
    // Fase 1 — validasi AST
    UnsupportedNode,   // node valid tapi belum dapat di-lowering
    ErrorNode,         // node placeholder hasil recovery parser
    MalformedShape,    // struktur AST tidak sah (child null di posisi wajib)

    // Fase 2 — lowering
    TypeMappingFailed,   // tipe semantic gagal dipetakan ke IR
    PhiInsertionFailed,  // join point tidak konsisten / versioning gagal
    TerminatorMissing,   // block selesai tanpa terminator
    ConstEncodeFailed,   // literal gagal masuk constant pool
    MatchNonSum,         // match/switch tag pada non-sum-type
};

// String stabil kode error, e.g. "ast.unsupported_node".
// Dipakai persis sebagai kode di pesan diagnostic.
// Definisi inline di header sengaja: layout modul (spesifikasi Bagian 8.1)
// tidak mencantumkan IrErrorCatalog.cpp — katalog murni data.
inline const char* astIrErrorCodeString(AstIrErrorCode code) {
    switch (code) {
    case AstIrErrorCode::UnsupportedNode:    return "ast.unsupported_node";
    case AstIrErrorCode::ErrorNode:          return "ast.error_node";
    case AstIrErrorCode::MalformedShape:     return "ast.malformed_shape";
    case AstIrErrorCode::TypeMappingFailed:  return "ast.type_mapping_failed";
    case AstIrErrorCode::PhiInsertionFailed: return "ast.phi_insertion_failed";
    case AstIrErrorCode::TerminatorMissing:  return "ast.terminator_missing";
    case AstIrErrorCode::ConstEncodeFailed:  return "ast.const_encode_failed";
    case AstIrErrorCode::MatchNonSum:        return "ast.match_non_sum";
    }
    return "ast.unknown";
}

// Deskripsi kondisi pemicu kode (untuk dokumentasi/tooling).
inline const char* astIrErrorDescription(AstIrErrorCode code) {
    switch (code) {
    case AstIrErrorCode::UnsupportedNode:
        return "node AST valid tapi belum dapat di-lowering";
    case AstIrErrorCode::ErrorNode:
        return "node placeholder hasil recovery parser lolos ke RhAST";
    case AstIrErrorCode::MalformedShape:
        return "struktur AST tidak sah (child null pada posisi wajib)";
    case AstIrErrorCode::TypeMappingFailed:
        return "tipe semantic gagal dipetakan ke IR";
    case AstIrErrorCode::PhiInsertionFailed:
        return "join point tidak konsisten / versioning gagal";
    case AstIrErrorCode::TerminatorMissing:
        return "block selesai tanpa terminator";
    case AstIrErrorCode::ConstEncodeFailed:
        return "literal gagal masuk constant pool";
    case AstIrErrorCode::MatchNonSum:
        return "match/switch tag dipakai pada non-sum-type";
    }
    return "kode error tidak dikenal";
}

} // namespace rhc
