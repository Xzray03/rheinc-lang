#pragma once

#include "RhAST/AstToIrOptions.h"
#include "RhAST/IrInstr.h"
#include "RhAST/IrModule.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rhc {

class DiagnosticEngine;

// IrBuilder — SATU-SATUNYA jalur mutasi IR (spesifikasi Bagian 2.5 & 7.5).
//
// Semua perubahan (insert/remove instruksi, ubah terminator, sisip phi,
// compact) dilakukan lewat API ini. IrBuilder menjaga invariant berikut
// pada SETIAP mutasi (spesifikasi Bagian 2.3):
//   1. value table per function valid (ValueId -> sumber SSA)
//   2. use-list (def-use) konsisten: tiap ValueId punya daftar
//      pemakaian (InstrId, slotOperand)
//   3. CFG edges sinkron: preds/succs tiap block sesuai terminator
//   4. symbol table lokal menunjuk versi terkini
//   5. epoch bump saat mutasi struktural
//   6. mode verify (opsional): cek konsistensi penuh setiap mutasi
//
// Violasi invariant karena bug internal -> Fatal via DiagnosticEngine
// lalu builder masuk keadaan broken(); semua API mutasi setelah itu
// menjadi no-op yang aman (mengembalikan ID Invalid / false).
class IrBuilder {
public:
    IrBuilder(IrModule& module, DiagnosticEngine& diagnostics,
              const AstToIrOptions& options);
    ~IrBuilder();

    IrBuilder(const IrBuilder&) = delete;
    IrBuilder& operator=(const IrBuilder&) = delete;

    const AstToIrOptions& options() const;

    // ===== Module-level interning =====
    // Tipe primitif didedup secara struktural; named type (Struct/Sum
    // recursive) SELALU dibuat entri baru (dedup struktural akan
    // merusak forward reference recursive).
    TypeId internType(const IrType& type);
    // Constant pool dengan dedup aktif: entri identik (kind + konten)
    // berbagi ConstId (spesifikasi Bagian 6.1).
    ConstId internConstant(const IrConstant& constant);

    // ===== Function & block =====
    FuncId createFunction(std::string name, TypeId fnType,
                          SourceLocation loc = 0);
    BlockId createBlock(FuncId func, std::string label = {},
                        SourceLocation loc = 0);
    void setInsertPoint(FuncId func, BlockId block);

    IrFunction& function(FuncId func);
    const IrFunction& function(FuncId func) const;
    IrBlock& block(FuncId func, BlockId block);
    const IrBlock& block(FuncId func, BlockId block) const;

    // Mendaftarkan value parameter fungsi (berurutan). Value param adalah
    // SSA value yang hidup di entry block — harus dibuat via builder agar
    // value table & use-list tetap konsisten.
    ValueId addParam(FuncId func, TypeId type, std::string name = {});

    // ===== Emission =====
    // Emisi generik: menambah instruksi di insert point saat ini,
    // mendaftarkan SSA value bila resultType != InvalidType, dan
    // memperbarui use-list serta CFG edges.
    // Instruksi terminator HARUS dilewatkan setTerminator().
    ValueId emit(IrOpcode op, TypeId resultType,
                 std::vector<IrOperand> operands,
                 SourceLocation loc = 0, std::string name = {});

    // Menetapkan terminator block. Invariant: tepat satu terminator di
    // posisi terakhir. Pemanggilan kedua pada block yang sama =
    // invariant internal (Fatal), kecuali replaceTerminator=true.
    void setTerminator(FuncId func, BlockId block, IrOpcode op,
                       std::vector<IrOperand> operands,
                       SourceLocation loc = 0, bool replaceTerminator = false);

    // ===== Phi (spesifikasi Bagian 4.2) =====
    ValueId createPhi(TypeId type, FuncId func, BlockId joinBlock,
                      SourceLocation loc = 0, std::string name = {});
    void addPhiIncoming(FuncId func, ValueId phi, BlockId pred, ValueId value);

    // ===== Penghapusan & compaction (spesifikasi Bagian 2.4) =====
    // Hapus dengan tombstone: op -> Dead; ID instruksi tidak berubah;
    // use-list dan struktur bantu diperbarui.
    void markDead(FuncId func, InstrId instr);
    // CompactFunction: hapus tombstone, remap seluruh ID, perbarui
    // use-list/preds/succs/symbol table/operand; epoch bump
    // (selama compaction seluruh ID lama tidak valid).
    void compact(FuncId func);

    // ===== Verifikasi =====
    // Cek konsistensi seluruh struktur bantu satu fungsi.
    bool verify(FuncId func, std::string* errorOut) const;
    bool verifyModule(std::string* errorOut) const;

    // ===== Use-list query (struktur bantu no. 2) =====
    struct Use {
        InstrId instr;
        std::uint32_t operandSlot;

        friend bool operator==(const Use& lhs, const Use& rhs) {
            return lhs.instr == rhs.instr && lhs.operandSlot == rhs.operandSlot;
        }
        friend bool operator!=(const Use& lhs, const Use& rhs) {
            return !(lhs == rhs);
        }
    };
    const std::vector<Use>& usesOf(FuncId func, ValueId value) const;

    // true setelah invariant internal dilanggar; translator harus abort.
    bool broken() const;

private:
    struct Impl;
    Impl* impl_;
};

} // namespace rhc
