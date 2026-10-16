#pragma once

#include <cstddef>

namespace rhc {

// Opsi translator AST -> IR (spesifikasi Bagian 7.1).
struct AstToIrOptions {
    // Ambang hybrid array literal (spesifikasi Bagian 6.3):
    // seluruh elemen konstan dan jumlah elemen > threshold
    // -> blob rata di constant pool (alloc + copy_const);
    // selain itu -> construct chain.
    std::size_t arrayBlobThreshold = 64;

    // Mode debug: verifikasi konsistensi seluruh struktur bantu
    // setelah tiap mutasi (spesifikasi Bagian 2.3 no. 6).
    bool verifyAfterEachMutation = false;
};

} // namespace rhc
