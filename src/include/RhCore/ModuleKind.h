#pragma once

// ============================================================
// ModuleKind & kebijakan entry point — DEFINISI TUNGGAL di RhCore.
//
// Semua modul (RhAST, RhIR, driver, tool, dsb.) WAJIB menaati aturan
// yang sama lewat helper di header ini; tidak boleh ada modul yang
// mengimplementasikan versi aturannya sendiri.
//
// Aturan:
//   - .rhc (Executable)     : dijalankan -> entry function TEPAT 1
//                             (tidak 0, tidak lebih dari 1).
//   - .rhh (Declaration)    : declaration-only, TIDAK dijalankan ->
//                             entry dilarang (harus 0).
// ============================================================

#include <cstdint>

namespace rhc {

enum class ModuleKind : std::uint8_t {
    Executable,   // .rhc — modul yang dijalankan
    Declaration,  // .rhh — declaration-only, tidak dijalankan
};

// Jumlah entry function yang dipersyaratkan untuk module kind ini.
// "Minimal 1 dan maksimal 1" untuk executable dinyatakan sebagai
// required == 1 (tepat satu); declaration required == 0 (tepat nol).
inline std::uint32_t requiredEntryCount(ModuleKind kind) {
    return kind == ModuleKind::Executable ? 1u : 0u;
}

inline const char* moduleKindName(ModuleKind kind) {
    return kind == ModuleKind::Executable ? "executable" : "declaration";
}

} // namespace rhc
