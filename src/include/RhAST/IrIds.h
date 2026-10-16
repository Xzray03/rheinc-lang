#pragma once

#include <cstdint>

namespace rhc {

// RhAST menggunakan ID berbasis indeks (std::uint32_t) untuk SEMUA referensi
// antar-entitas IR. Pointer mentah ke dalam container dilarang, karena
// container dapat reallocate saat modul lain memutasi IR.
// Lihat RhAST_Spesifikasi.md Bagian 2.1.
using InstrId   = std::uint32_t;
using BlockId   = std::uint32_t;
using ValueId   = std::uint32_t;
using FuncId    = std::uint32_t;
using ConstId   = std::uint32_t;
using TypeId    = std::uint32_t;
using OperandId = std::uint32_t;

// Konvensi nilai khusus: 0xFFFFFFFF berarti "tidak valid / belum di-set".
inline constexpr InstrId   InvalidInstr   = 0xFFFFFFFFu;
inline constexpr BlockId   InvalidBlock   = 0xFFFFFFFFu;
inline constexpr ValueId   InvalidValue   = 0xFFFFFFFFu;
inline constexpr FuncId    InvalidFunc    = 0xFFFFFFFFu;
inline constexpr ConstId   InvalidConst   = 0xFFFFFFFFu;
inline constexpr TypeId    InvalidType    = 0xFFFFFFFFu;
inline constexpr OperandId InvalidOperand = 0xFFFFFFFFu;

} // namespace rhc
