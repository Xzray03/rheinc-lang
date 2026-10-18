// RhLinker.h - Linker Rheinc: object user + library RhBuiltin -> executable.
//
// Seleksi object runtime "hanya yang dipakai": baca simbol undefined dari
// object user, bangun indeks manifest simbol->object dari library dir
// (nm), ambil closure transitif, invoke ld. Mode sharedLibrary memakai
// ld -shared tanpa CRT startup untuk target Dynamic Library.

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
    // Object user hasil RhBackend. Minimal satu; urutan dipertahankan untuk ld.
    // Untuk target Executable biasanya object hasil compile. Untuk target
    // Dynamic Library boleh juga berisi dependency .o/.a/.so.
    std::vector<std::string> inputObjects;
    std::string libraryDir;     // direktori object RhBuiltin (wajib)
    std::string outputPath;     // executable/shared library hasil (wajib)
    std::string linker = "ld";  // program linker
    // true: link dengan `ld -shared`; jangan sertakan CRT startup.
    bool sharedLibrary = false;
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
