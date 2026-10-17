#pragma once

#include "RhIR/IrInstr.h"
#include "RhIR/IrIds.h"
#include "RhCore/SourceLocation.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rhc {

// Value table per function (spesifikasi Bagian 2.3 no. 1).
// Setiap SSA value adalah entri IrValue; `payload` menunjuk sumber nilai:
//   Param -> indeks parameter (indeks ke IrFunction::params)
//   Instr -> InstrId penghasil (instruksi non-terminator dengan result)
struct IrValue {
    enum class Kind : std::uint8_t {
        Param,
        Instr,
    };

    Kind kind = Kind::Instr;
    std::uint32_t payload = 0;   // indeks param atau InstrId penghasil
    TypeId type = InvalidType;   // tipe eksplisit per operand (prinsip verbose)
    std::string name;            // nama tampil penuh "x.3"; kosong = anonim
                                 // (penomoran fallback saat dump)
};

// Basic block (spesifikasi Bagian 2.2).
// Invariant: tepat satu terminator (isTerminator(op)) sebagai instruksi
// terakhir — dijaga oleh IrBuilder dan divalidasi verify/self-check.
struct IrBlock {
    std::string label;                  // block anonim boleh kosong
    std::vector<InstrId> insts;         // instruksi urut; terminator = terakhir
    std::vector<BlockId> preds;         // struktur bantu CFG
    std::vector<BlockId> succs;         // struktur bantu CFG
    SourceLocation loc = 0;
    std::uint32_t epoch = 0;
};

// Fungsi IR (spesifikasi Bagian 2.2).
//
// Kelengkapan desain di luar struktur literal spesifikasi: instruksi
// disimpan sebagai TABEL FLAT per fungsi (`instrs`). IrBlock hanya
// menyimpan IrBlock::insts berupa InstrId ke tabel ini — InstrId jadi
// unik per fungsi, sehingga value table dan use-list (InstrId, slot)
// tidak ambigu antar block.
//
// Struktur bantu yang di-maintain incremental (Bagian 2.3):
//   values  -> value table (ValueId -> sumber SSA)
//   locals  -> symbol table lokal (nama -> versi terkini ValueId)
//   params  -> value parameter, berurutan
//   epoch   -> bump saat mutasi struktural (deteksi ID stale saat debugging)
enum class IrFunctionKind : std::uint8_t {
    Normal,
    External,
    GlobalInit,
    Entry,
};

struct IrFunction {
    std::string name;
    TypeId fnType = InvalidType;
    IrFunctionKind kind = IrFunctionKind::Normal;
    std::vector<IrInstr> instrs;        // tabel instruksi flat (milik fungsi)
    std::vector<IrBlock> blocks;
    std::vector<IrValue> values;
    std::unordered_map<std::string, ValueId> locals;  // symbol table lokal
    std::vector<ValueId> params;
    SourceLocation loc = 0;
    std::uint32_t epoch = 0;

    // true bila fungsi External ini adalah STUB BUILTIN/RUNTIME bahasa
    // (dibuat otomatis translator untuk pemanggilan fungsi yang tidak
    // dideklarasikan di modul, mis. tampilkan/panjang). Stub builtin
    // di-link ke runtime bahasa — bukan ke definisi user — sehingga
    // IrLinkVerifier tidak melaporkannya sebagai eksternal tak
    // terdefinisi. Prototipe user dari .rhh tetap false.
    bool isBuiltin = false;
};

} // namespace rhc
