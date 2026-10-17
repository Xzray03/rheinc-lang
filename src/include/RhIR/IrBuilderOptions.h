#pragma once

namespace rhc {

// Opsi IrBuilder (RhIR). Sengaja terpisah dari AstToIrOptions (RhAST)
// agar builder IR tidak bergantung pada modul front-end: RhAST meneruskan
// flag yang relevan lewat bridging di translator.
struct IrBuilderOptions {
    // Mode debug: verifikasi konsistensi seluruh struktur bantu
    // setelah tiap mutasi (spesifikasi Bagian 2.3 no. 6).
    bool verifyAfterEachMutation = false;
};

} // namespace rhc
