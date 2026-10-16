#pragma once

#include "RhAST/IrIds.h"

#include <cstdint>
#include <vector>

namespace rhc {

// Constant pool per module (spesifikasi Bagian 6).
// Pool read-only: tidak ada mutasi entri constant. Kebutuhan mutable
// -> alloc + copy_const. Referensi operand via ConstId
// (IrOperandKind::ConstantV). Entri identik (jenis + konten) berbagi
// ConstId (dedup aktif, dijaga oleh IrBuilder saat interning).
enum class IrConstantKind : std::uint8_t {
    String,     // bytes UTF-8 + length eksplisit; null-terminator
                // opsional/terpisah (keputusan backend)
    BigInt,     // byte array, little-endian, two's complement
    FloatBits,  // bit pattern canonical (f32/f64, lebar dari type);
                // teks desimal asli disimpan opsional sebagai metadata
                // debug di luar struktur ini
    ArrayBlob,  // data rata (blob) array literal homogen konstan;
                // representasi IR-nya: alloc + copy_const
};

struct IrConstant {
    IrConstantKind kind;
    TypeId type = InvalidType;              // tipe IR nilai konstanta
    std::vector<std::uint8_t> bytes;        // String / BigInt / ArrayBlob
    std::uint64_t bitPattern = 0;           // FloatBits (canonical)
    std::uint64_t length = 0;               // String: jumlah byte;
                                            // ArrayBlob: jumlah elemen
};

} // namespace rhc
