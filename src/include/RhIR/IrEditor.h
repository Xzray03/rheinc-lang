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

    // UnreachableBlockElim LENGKAP, iterasi internal sampai stabil:
    //   1) Fold CondBr ber-kondisi-konstan (ConstBool/ImmV) menjadi Br
    //      ke target terambil; edge ke target terbuang dihapus dari
    //      succs/preds DAN incoming phi-nya (operand phi dipetakan per
    //      posisi preds).
    //   2) Jangkauan dari block entry (blocks[0]) lewat succs; block
    //      tak terjangkau beserta instruksinya dibuang.
    //   3) Orphan rescue: def instruksi yang dirujuk operand instruksi
    //      HIDUP tidak dibuang (tetap di tabel, tak ter-urut) agar tak
    //      ada ValueV menggantung.
    //   4) Padatkan blocks/instrs/values; remap preds/succs, operand
    //      BlockV, payload fn.values, operand ValueV, params, locals.
    // Aman tanpa renumber antar-fungsi: BlockId/InstrId/ValueId lokal
    // per fungsi. Block entry sendiri selamat oleh definisi.
    // foldedCondBr menerima jumlah CondBr konstan yang ter-fold menjadi
    // Br (mutasi IR nyata walau tanpa block terhapus) — pass wajib
    // melaporkan changed bila removedBlocks > 0 ATAU foldedCondBr > 0.
    IrEditorReport removeUnreachableBlocks(FuncId func,
                                           std::uint32_t& removedBlocks,
                                           std::uint32_t& foldedCondBr);

    // Menyisipkan instruksi baru tepat SEBELUM posIndex dalam urutan
    // block. Instruksi ditambahkan di UJUNG tabel instruksi (InstrId
    // baru; tak ada id lama yang berubah) dan SSA value terdaftar bila
    // resultType valid. Index-preserving penuh: tak ada instruksi/value
    // lama yang berpindah, hanya APPEND + sisip urutan block (lokal).
    IrEditorReport insertInstrBefore(FuncId func, BlockId block,
                                     std::size_t posIndex,
                                     const IrInstr& instr, ValueId& outValue);

    // Menambah block kosong di ujung tabel block fungsi (BlockId baru;
    // index-preserving karena BlockId function-lokal). Dipakai pass yang
    // butuh CFG surgery: clone block (loop unroll), guard, remainder.
    IrEditorReport appendBlock(FuncId func, BlockId& outBlock);

    // Menambah operand di ujung instruksi (index-preserving: hanya
    // push_back ke operands; panjang operands instruksi lain tak berubah).
    // Dipakai pass yang perlu menambah pasangan incoming phi [BlockV,
    // ValueV] tanpa membuat instruksi baru.
    IrEditorReport appendOperandAt(FuncId func, BlockId block,
                                   std::size_t posIndex, IrOperand operand);

    // Menghapus SEKELompok instruksi dari fungsi: `dead[iid]==true` dibuang
    // dari urutan block + tabel dipadatkan + SEMUA referensi function-lokal
    // di-remap (via compactFunctionTables). Untuk instruksi NON-MURNI yang
    // terbukti mati secara semantik (mis. Alloc/Store non-escaping hasil
    // escape analysis) -- DCE whitelist hanya op murni.
    IrEditorReport eliminateInstrs(FuncId func, const std::vector<bool>& dead);

    // Menghapus SEKELompok slot global dari modul: `dead[i]==true` dibuang
    // dari IrModule::globalVars; SEMUA operand GlobalV di semua fungsi
    // di-remap ke indeks baru. Instruksi yang masih merujuk slot yang
    // dihapus = kesalahan pemakaian -> report error (Invariant broken).
    // Dipakai pass StaticVariableUnification setelah semua Load/Store
    // global terkait dihapus.
    IrEditorReport removeDeadGlobals(const std::vector<bool>& dead);

    // Menggabungkan block `succ` ke `pred`: syarat pred.succs == {succ}
    // dan succ.preds == {pred} (caller memeriksa) dan succ bukan entry.
    // Instruksi succ dipindahkan ke pred (terminator pred dibuang), SSA
    // value tetap (InstrId tidak berubah); semua operand BlockV yang
    // menunjuk succ di-remap ke pred (termasuk phi incoming di block
    // lain); succ menjadi orphan kosong -> disapu removeUnreachableBlocks
    // oleh pass pemanggil. Satu touch epoch.
    IrEditorReport mergeBlocks(FuncId func, BlockId pred, BlockId succ);

    // Menghapus SEKELompok fungsi dari modul: `dead[i]==true` dibuang
    // dari IrModule::functions; SEMUA operand FuncV (Call [0], AddrOf
    // [0]) di semua fungsi di-remap ke indeks baru. Entry & GlobalInit
    // tidak boleh ditandai (caller menjamin). Dipakai pass
    // DeadFunctionElimination.
    IrEditorReport removeDeadFunctions(const std::vector<bool>& dead);

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
