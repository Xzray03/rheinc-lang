// RhBackendError.h - Error catalog RhBackend.
//
// Semua kegagalan codegen dikemas ke RhBackendError agar konsumen
// (RheincMain, RhLinker nanti) punya kontrak diagnostik yang stabil.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rhc {

enum class RhBackendErrorCode : std::uint8_t {
    None = 0,
    UnsupportedTarget,   // target bukan x86_64-linux-sysv
    UnsupportedOpcode,   // opcode IR belum didukung Fase 0
    UnsupportedOperand,  // bentuk operand belum didukung Fase 0
    UnsupportedType,     // tipe belum didukung Fase 0
    EmitFailure,         // gagal menulis file assembly
    AssemblerFailed,     // 'as' mengembalikan status non-zero
};

struct RhBackendError {
    RhBackendErrorCode code = RhBackendErrorCode::None;
    std::string functionName;   // kosong bila di luar fungsi
    std::string message;
};

struct RhBackendReport {
    bool ok = false;
    std::vector<RhBackendError> errors;
};

}  // namespace rhc
