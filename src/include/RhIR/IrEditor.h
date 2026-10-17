#pragma once

#include "RhIR/IrIds.h"
#include "RhIR/IrInstr.h"
#include "RhIR/IrVerifier.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rhc {

class IrModule;
struct IrFunction;
struct IrInstr;
struct IrConstant;

// ============ IrEditor: mutasi/normalisasi IR (RhIR) ============
//
// IrEditor menyediakan primitif mutasi TERKENDALI atas IrModule.
// Desain irisan 1 sengaja INDEX-PRESERVING: semua operasi hanya
// menimpa (replace) pada indeks yang sudah ada atau menambah di ujung
// tabel. Tidak ada penghapusan/penyisipan di tengah, sehingga SEMUA
// referensi berbasis indeks (operand value/const/type id, block preds/
// succs, globalInits) tetap valid tanpa renumber. Fase 2 akan
// menyediakan operasi yang menyebabkan renumber dengan remapping penuh.
//
// Setiap operasi mengembalikan IrEditorReport; kesalahan indeks atau
// tipe ditolak sebelum modul disentuh. Opsi verifyEachMutation
// menjalankan ulang verifier sesuai verifyLevel setelah tiap mutasi
// yang berhasil diterapkan.

enum class IrEditorErrorCode : std::uint8_t {
    None,
    NullModule,
    InvalidFunction,
    InvalidBlock,
    InvalidInstrPosition,   // posIndex >= block.insts.size()
    InvalidOperandIndex,    // operandIndex >= instr.operands.size()
    InvalidConst,
    InvalidTypeRef,         // TypeId merujuk di luar module.types
    ResultTypeMismatch,     // replaceInstr mengubah resultType
    ConstTypeMismatch,      // replaceConst dengan tipe berbeda
    VerifyFailed,           // verifyEachMutation/validate: verifier menolak
};

struct IrEditorError {
    IrEditorErrorCode code;
    std::string message;
};

struct IrEditorReport {
    bool ok = false;
    IrEditorError error{IrEditorErrorCode::None, {}};
};

struct IrEditorOptions {
    // Jalankan ulang verifier setelah tiap mutasi yang berhasil.
    bool verifyEachMutation = false;
    // Level verify untuk verifyEachMutation (Structural tidak butuh
    // target provider; Type untuk cek tipe operand/result).
    IrVerifyLevel verifyLevel = IrVerifyLevel::Structural;
};

// Whitelist op MURNI untuk DCE (tanpa efek samping yang teramati IR):
// konstanta SSA, aritmetika/bitwise/perbandingan/logika murni, kast,
// dan konstruksi/ekstraksi agregat first-class. Op memori (Load/Store/
// Alloc), panggilan, Len (baca memori), Inc/Dec (mutasi), dan semua
// terminator TIDAK pernah dihapus DCE meski SSA value-nya tak terpakai.
bool irOpIsPureForDce(IrOpcode op);

class IrEditor {
public:
    explicit IrEditor(IrModule& module, IrEditorOptions options = {});
    ~IrEditor();
    IrEditor(const IrEditor&) = delete;
    IrEditor& operator=(const IrEditor&) = delete;

    // --- mutasi instruksi (index-preserving) ---

    // Timpa instruksi pada posisi ke-posIndex dalam block. InstrId
    // TETAP; resultType harus sama dengan instruksi lama (ganti tipe
    // result lewat setInstrResultType agar eksplisit). epoch ditambah.
    IrEditorReport replaceInstr(FuncId func, BlockId block,
                                std::size_t posIndex, const IrInstr& instr);

    // Timpa operand ke-operandIndex. Indek operand tetap; panjang
    // operands tidak berubah.
    IrEditorReport replaceOperandAt(FuncId func, BlockId block,
                                    std::size_t posIndex,
                                    std::size_t operandIndex,
                                    IrOperand operand);

    // Ubah tipe result instruksi (dan tipe SSA value terkait).
    IrEditorReport setInstrResultType(FuncId func, BlockId block,
                                      std::size_t posIndex, TypeId type);

    // --- mutasi constant pool (index-preserving) ---

    // Timpa nilai konstanta; kind dan type HARUS sama (referensi
    // operand ke ConstId tetap valid secara tipe).
    IrEditorReport replaceConst(ConstId id, const IrConstant& constant);

    // Tambah konstanta di ujung pool (aman; tidak merubah id lama).
    IrEditorReport appendConst(const IrConstant& constant, ConstId& outId);

    // --- fase 2: DCE + compaction (function-lokal) ---

    // Menghapus instruksi MURNI yang SSA value-nya tidak dipakai
    // instruksi lain, lalu memadatkan tabel instrs/values fungsi dan
    // me-remap SEMUA referensi function-lokal: blk.insts, payload
    // fn.values, operand ValueV, fn.params, dan fn.locals.
    //
    // Aman tanpa renumber antar-fungsi: ValueId/InstrId/BlockId
    // bersifat lokal per fungsi. Whitelist op murni sengaja konservatif
    // (ConstInt/Add/Sub/Mul — op tanpa efek samping); op lain tidak
    // pernah dihapus meski SSA value-nya tak terpakai.
    IrEditorReport removeDeadInstrs(FuncId func, std::uint32_t& removedCount);

    // Menghapus block yang TIDAK TERJANGKAU dari block entry (blocks[0])
    // lewat rantai succs, beserta seluruh instruksi yang hanya ter-urut
    // di block-block itu, lalu memadatkan tabel blocks/instrs/values dan
    // me-remap SEMUA referensi function-lokal: preds/succs, operand
    // BlockV (target cabang), payload fn.values, operand ValueV,
    // fn.params, dan fn.locals.
    //
    // Aman tanpa renumber antar-fungsi: BlockId/InstrId/ValueId lokal
    // per fungsi. Block entry sendiri selamat oleh definisi.
    IrEditorReport removeUnreachableBlocks(FuncId func,
                                           std::uint32_t& removedBlocks);

    // --- utilitas ---

    IrEditorReport validate(IrVerifyLevel level,
                            const IrTargetProvider* targetProvider = nullptr) const;
    IrModule& module() const noexcept { return *module_; }
    const IrEditorOptions& options() const noexcept { return options_; }

private:
    IrModule* module_;
    IrEditorOptions options_;

    IrInstr* locateInstr(FuncId func, BlockId block, std::size_t posIndex,
                         IrEditorReport& report) const;
    // Padatkan tabel instrs/values fungsi terhadap deadInstrs (true =
    // hapus) dan remap SEMUA referensi function-lokal. Dipakai bersama
    // oleh removeDeadInstrs dan removeUnreachableBlocks.
    IrEditorReport compactFunctionTables(IrFunction& fn,
                                         const std::vector<bool>& deadInstrs);
    IrEditorReport afterMutation(const char* opName);
};

} // namespace rhc
