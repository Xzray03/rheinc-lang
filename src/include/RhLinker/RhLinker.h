// RhLinker.h - Linker Rheinc: object user + library RhBuiltin -> executable.
//
// Seleksi object runtime "hanya yang dipakai": baca simbol undefined dari
// object user, bangun indeks manifest simbol->object dari library dir
// (nm), ambil closure transitif, invoke ld. Static/Dynamic library:
// placeholder (direncanakan).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rhc {

enum class RhLinkerErrorCode : std::uint8_t {
    None = 0,
    MissingInput,     // object user tak ada / kosong
    LibraryNotFound,  // direktori library RhBuiltin invalid
    SymbolScanFailed, // nm gagal pada object/library
    UnresolvedSymbol, // simbol rh_* tak ditemukan di library
    LdFailed,         // ld status non-zero
};

struct RhLinkerError {
    RhLinkerErrorCode code = RhLinkerErrorCode::None;
    std::string message;
};

struct RhLinkerOptions {
    std::string inputObject;    // object hasil RhBackend (wajib)
    std::string libraryDir;     // direktori object RhBuiltin (wajib)
    std::string outputPath;     // executable hasil (wajib)
    std::string linker = "ld";  // program linker
};

struct RhLinkerReport {
    bool ok = false;
    std::vector<std::string> selectedObjects;  // object runtime terpilih
    std::vector<RhLinkerError> errors;
};

class RhLinker {
public:
    RhLinkerReport link(const RhLinkerOptions& options) const;
};

}  // namespace rhc
