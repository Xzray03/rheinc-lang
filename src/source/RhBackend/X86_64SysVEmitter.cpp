// X86_64SysVEmitter.cpp - Implementasi emitter (Fase 2).
//
// Model codegen: NAIVE stack-slot (Q11) — tiap SSA value berupa slot
// 8-byte di frame; register hanya scratch sementara. Struktur siap
// diganti RegAlloc (NaiveRegAlloc/LinearScan) tanpa mengubah API.
//
// Cakupan Fase 2:
//   - Konstanta: ConstInt/ConstBool/ConstString
//   - Aritmetika/bitwise: Add Sub Mul Div DivFloor Mod, BitAnd BitOr
//     BitXor Shl Shr, Neg BitNot Inc Dec
//   - Logika: Cmp* (setcc), Not
//   - Kontrol alur: Br CondBr SwitchInt (cmp chain), Phi (edge moves)
//   - Cast int->int (movsx/movzx); Call builtin (arg int ValueV/ImmV
//     + string); Ret/RetVoid
// Di luar cakupan -> UnsupportedOpcode/Operand (fail-fast).

#include "RhBackend/X86_64SysVEmitter.h"

#include "RhBackend/RegAlloc.h"

#include <cstdlib>

#include "RhBackend/AsmBuilder.h"
#include "RhBackend/RuntimeContract.h"

#include "RhIR/IrConstant.h"
#include "RhIR/IrModule.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rhc {

namespace {

void fail(std::vector<RhBackendError>& errors, RhBackendErrorCode code,
          const std::string& fnName, const std::string& msg) {
    RhBackendError err;
    err.code = code;
    err.functionName = fnName;
    err.message = msg;
    errors.push_back(std::move(err));
}

std::string globalLabelName(const IrModule& module, std::uint32_t gid) {
    std::string n = module.globalVars[gid].name;
    for (char& c : n) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok) c = '_';
    }
    return "rh_g_" + n;
}

struct PendingString {
    std::string label;
    IrConstant constant;
};

class FnEmitter {
public:
    FnEmitter(const IrModule& module, const IrFunction& fn, AsmBuilder& out,
              std::vector<RhBackendError>& errors)
        : module_(module), fn_(fn), out_(out), errors_(errors) {}

    void setRegAlloc(const RegAllocResult* alloc) { alloc_ = alloc; }

    // Home register untuk value (nullptr bila spill/slot).
    const RegLoc* home(ValueId v) const {
        if (alloc_ == nullptr) return nullptr;
        const auto it = alloc_->homes.find(v);
        return it == alloc_->homes.end() ? nullptr : &it->second;
    }

    bool run(std::vector<PendingString>& pendingStrings,
             std::vector<std::pair<std::string, std::uint64_t>>& pendingFloats) {
        pendingStrings_ = &pendingStrings;
        pendingFloats_ = &pendingFloats;
        buildIndices();
        if (typeFailed_) {
            fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                 "value bertipe >16 byte / non-View belum didukung Fase 3");
            return false;
        }

        // DEVIATIONS: nama IR .__global_init diawali titik (label lokal di IR);
        // sanitize asmName() mengubah '.' jadi '_' -> ___global_init tidak cocok
        // dengan referensi __global_init dari rh_crt.s. GlobalInit dipetakan ke
        // label __global_init eksplisit agar link ke CRT stub benar.
        const std::string fnLabel =
            (fn_.kind == IrFunctionKind::GlobalInit)
                ? std::string("__global_init")
                : FnEmitter::asmName(fn_.name);
        out_.globl(fnLabel);
        out_.typeFunction(fnLabel);
        out_.align_text();  // entry fungsi 16-byte aligned (ABI/performa)
        out_.label(fnLabel);
        out_.ins("pushq %rbp");
        out_.ins("movq %rsp, %rbp");
        // RegAlloc graph: simpan callee-saved yang terpakai.
        if (alloc_ != nullptr) {
            for (const std::string& r : alloc_->usedCalleeSaved) {
                out_.ins("pushq %" + r);
            }
        }
        if (frameSize_ > 0) {
            out_.ins("subq $" + std::to_string(frameSize_) + ", %rsp");
        }
        if (!emitParamMoves()) {
            return false;
        }

        for (std::uint32_t b = 0; b < fn_.blocks.size(); ++b) {
            if (!emitBlock(b)) {
                return false;
            }
        }
        // Ukuran simbol fungsi (ELF hygiene: symtab benar untuk debugger
        // & linker, bukan hanya "cukup untuk jalan").
        out_.size_sym(fnLabel, ".-" + fnLabel);
        return true;
    }

private:
    // ---- indexing: instr/value/block ordinal -> slot/label ----
    void buildIndices() {
        // RegAlloc graph coloring: register yang terpakai -> prolog push
        // callee-saved; slot region digeser ke bawah area saved-regs.
        if (alloc_ != nullptr) {
            shift_ = 8 * static_cast<std::uint32_t>(
                             alloc_->usedCalleeSaved.size());
            // Selaraskan ke kelipatan 16: SEMUA slot (termasuk slot
            // Float 16-byte dari allocSlot) digeser -shift_ di bawah;
            // shift_ bukan kelipatan 16 membuat slot Float movdqa
            // misaligned relatif rbp (rbp 16-aligned sjk fix stub
            // __rheinc_stub). Konsisten dengan frameSize_ (baris ~181).
            shift_ = (shift_ + 15) / 16 * 16;
        }
        for (ValueId i = 0; i < fn_.values.size(); ++i) {
            const IrValue& v = fn_.values[i];
            if (v.kind != IrValue::Kind::Instr || v.payload >= fn_.instrs.size()) {
                continue;
            }
            valueOfInstr_.emplace(v.payload, i);
            if (home(i) != nullptr) continue;  // tinggal di register
            // Alokasi slot mengikuti tipe VALUE, TAPI juga tipe hasil
            // instruksi: pasca-optimizer keduanya bisa berbeda (CSE/dst),
            // dan consumer movdqa mengecek instr.resultType. Bila salah
            // satu Float -> alokasikan sebagai float (16-byte aligned).
            TypeId at = v.type;
            if (!isFloat(module_, at) &&
                fn_.instrs[v.payload].resultType != InvalidType &&
                isFloat(module_, fn_.instrs[v.payload].resultType)) {
                at = fn_.instrs[v.payload].resultType;
            }
            slotOfValue_.emplace(i, allocSlot(at));
        }
        // Parameter fungsi (bukan instruksi) ikut dapat slot; diisi dari
        // register argumen System V di prolog (emitParamMoves).
        for (const ValueId prm : fn_.params) {
            if (prm < fn_.values.size() && home(prm) == nullptr) {
                slotOfValue_.emplace(prm, allocSlot(fn_.values[prm].type));
            }
        }
        // Geser seluruh slot di bawah area callee-saved tersimpan.
        if (shift_ > 0) {
            for (auto& kv : slotOfValue_) kv.second -= shift_;
        }
        // Area alloc statis di frame: tiap instruksi Alloc @T mengambil
        // [quad: panjang bytes data][data ...] (16-byte align). Nilai
        // Alloc = alamat DATA (header di -8). Konvensi ini sama dengan
        // heap RhBuiltin (rh_alloc) sehingga Len = load header.
        for (const IrInstr& in : fn_.instrs) {
            if (in.op != IrOpcode::Alloc) continue;
            if (in.operands.empty() ||
                in.operands[0].kind != IrOperandKind::TypeV ||
                in.operands[0].id >= module_.types.size()) {
                typeFailed_ = true;
                continue;
            }
            allocArea_ += 8 + typeSizeAt(in.operands[0].id);
            allocArea_ = (allocArea_ + 15) / 16 * 16;
        }
        // Scratch 16-byte utk spill argumen baca yang ber-register-home
        // (rh_read menulis via alamat; home value harus spill+reload).
        bacaScratch_ = -8 * (static_cast<std::int32_t>(slotCount_) + 2);
        slotCount_ += 2;
        frameSize_ = (slotCount_ * 8 + allocArea_ + shift_ + 15) / 16 * 16;
    }

    // Slot 8-byte per SSA value (naive). View {ptr,len} = dua slot
    // berurutan 16-byte aligned (slot pertama = ptr, kedua = len).
    // Tipe >16 byte / non-View -> tandai typeFailed_ (bail di run()).
    static bool isFloat(const IrModule& m, TypeId t) {
        return t != InvalidType && t < m.types.size() &&
               m.types[t].kind == IrTypeKind::Float && m.types[t].sizeBytes == 8;
    }

    // Simbol runtime printer sesuai tipe nilai: logika -> rh_print_bool,
    // integer unsigned -> rh_print_uint, karakter (Int 1-byte bertag
    // "karakter" dari MapSemanticType) -> rh_print_char, sisanya
    // (integer signed) -> rh_print_int.
    static const char* printSymForType(const IrModule& m, TypeId t) {
        if (t == InvalidType || t >= m.types.size()) return "rh_print_int";
        const IrType& ty = m.types[t];
        if (ty.kind == IrTypeKind::Bool) return "rh_print_bool";
        // karakter DIPERIKSA DULUAN: tipe karakter = int UNSIGNED width=4,
        // tanpa reorder ia tertangkap cabang rh_print_uint.
        // Tag "karakter" eksklusif (hanya di-set MapSemanticType utk Karakter).
        if (ty.kind == IrTypeKind::Int && ty.name == "karakter") {
            return "rh_print_char";
        }
        if (ty.kind == IrTypeKind::Int && !ty.isSigned) return "rh_print_uint";
        return "rh_print_int";
    }

    // Nama register ber-suffix lebar untuk movsx/movzx: reg "rbx" ->
    // "ebx"/"bx"/"bl", reg "r12" -> "r12d"/"r12w"/"r12b". Hanya utk
    // register HOME regalloc (rbx, r12-r15 + keluarga ax..di).
    static std::string regSized(const std::string& reg, std::uint32_t bytes) {
        static const char* full[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi"};
        static const char* d32[]  = {"eax","ecx","edx","ebx","esp","ebp","esi","edi"};
        static const char* d16[]  = {"ax","cx","dx","bx","sp","bp","si","di"};
        static const char* d8[]   = {"al","cl","dl","bl","spl","bpl","sil","dil"};
        for (int i = 0; i < 8; ++i) {
            if (reg == full[i]) {
                return bytes == 1 ? d8[i] : (bytes == 2 ? d16[i] : d32[i]);
            }
        }
        // r8..r15: suffix langsung (r12d/r12w/r12b).
        if (reg.size() >= 2 && reg[0] == 'r' &&
            reg[1] >= '0' && reg[1] <= '9') {
            return reg + (bytes == 1 ? "b" : (bytes == 2 ? "w" : "d"));
        }
        return reg;
    }

    // Tipe 16-byte inline (2 slot / 2 register gp): View {ptr,len},
    // Array kecil, Struct kecil. Nilai tinggal di slot berurutan.
    static bool isFat16(const IrModule& m, TypeId t) {
        if (t == InvalidType || t >= m.types.size()) return false;
        const std::uint64_t sz = m.types[t].sizeBytes;
        if (sz <= 8 || sz > 16) return false;
        const IrTypeKind k = m.types[t].kind;
        return k == IrTypeKind::View || k == IrTypeKind::Array ||
               k == IrTypeKind::Struct;
    }

    std::int32_t allocSlot(TypeId t) {
        const std::uint64_t sz = typeSizeAt(t);
        // Float f64: SATUAN VEKTOR 16-byte (2 slot, align 16). HARUS
        // sebelum cek sz<=8 (Float size 8 — jangan sampai jadi dead code).
        if (t != InvalidType && t < module_.types.size() &&
            module_.types[t].kind == IrTypeKind::Float) {
            // Align 16: off = -8*(slotCount+1) kelipatan 16 <=> slotCount GANJIL.
            if (slotCount_ % 2 == 0) ++slotCount_;
            const std::int32_t off =
                -8 * (static_cast<std::int32_t>(slotCount_) + 1);
            slotCount_ += 2;
            return off;
        }
        if (sz <= 8) {
            const std::int32_t off =
                -8 * (static_cast<std::int32_t>(slotCount_) + 1);
            ++slotCount_;
            return off;
        }
        // Float f64: SATUAN VEKTOR 16-byte (2 slot, align 16). Operasi
        // memakai instruksi packed (*pd) dengan akses movdqa — alignmen
        // 16-byte dijamin invariant: rbp 16-aligned setelah push rbp,
        // dan slot float selalu di offset kelipatan 16.
        if (t != InvalidType && t < module_.types.size() &&
            module_.types[t].kind == IrTypeKind::Float) {
            // Align 16: off = -8*(slotCount+1) kelipatan 16 <=> slotCount GANJIL.
            if (slotCount_ % 2 == 0) ++slotCount_;
            const std::int32_t off =
                -8 * (static_cast<std::int32_t>(slotCount_) + 1);
            slotCount_ += 2;
            return off;
        }
        // Agregat inline (View/Array/Struct/Sum): N = ceil(sz/8) slot
        // berurutan, align 16. Elemen pertama di offset paling negatif.
        if (t != InvalidType && t < module_.types.size() &&
            (module_.types[t].kind == IrTypeKind::View ||
             module_.types[t].kind == IrTypeKind::Array ||
             module_.types[t].kind == IrTypeKind::Struct ||
             module_.types[t].kind == IrTypeKind::Sum)) {
            const std::uint32_t n =
                static_cast<std::uint32_t>((sz + 7) / 8 == 0 ? 1 : (sz + 7) / 8);
            while (slotCount_ % 2 != 0 || (n % 2 == 0 && slotCount_ % 2 != 0)) {
                ++slotCount_;
            }
            // Align 16: off = -8*(slotCount+1) kelipatan 16 <=> slotCount GANJIL.
            if (slotCount_ % 2 == 0) ++slotCount_;  // align 16
            const std::int32_t off =
                -8 * (static_cast<std::int32_t>(slotCount_) + 1);
            slotCount_ += n;
            return off;
        }
        typeFailed_ = true;
        return 0;
    }

    // Jumlah slot (8-byte unit) untuk value bertipe t.
    std::uint32_t slotUnitsOf(TypeId t) const {
        if (isFloat(module_, t)) return 2;  // satuan vektor 16-byte
        const std::uint64_t sz = typeSizeAt(t);
        if (sz <= 8) return 1;
        return static_cast<std::uint32_t>((sz + 7) / 8);
    }

    // Prolog: pindahkan argumen register System V ke slot param.
    // Epilog: pulihkan callee-saved (dalam urutan terbalik) lalu leave/ret.
    void emitEpilogue() {
        if (alloc_ != nullptr && !alloc_->usedCalleeSaved.empty()) {
            out_.ins("addq $" + std::to_string(frameSize_) + ", %rsp");
            for (auto it = alloc_->usedCalleeSaved.rbegin();
                 it != alloc_->usedCalleeSaved.rend(); ++it) {
                out_.ins("popq %" + *it);
            }
        }
        out_.ins("leave");
        out_.ins("retq");
    }

    bool emitParamMoves() {
        static const char* kGp[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        std::uint32_t reg = 0;
        std::uint32_t si = 0;  // stack arg index
        std::uint32_t xm = 0;  // register xmm index
        for (const ValueId prm : fn_.params) {
            const auto s = slotOfValue_.find(prm);
            // Register home valid meski tak punya slot (alloc skip slot).
            if (s == slotOfValue_.end() && home(prm) == nullptr) continue;
            const TypeId pt = prm < fn_.values.size() ? fn_.values[prm].type
                                                      : InvalidType;
            if (isFloat(module_, pt)) {
                if (xm >= 8) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "param float melebihi 8 register xmm");
                    return false;
                }
                out_.ins("movdqa %xmm" + std::to_string(xm) + ", " +
                         std::to_string(s->second) + "(%rbp)");
                ++xm;
                continue;
            }
            const std::uint64_t sz = typeSizeAt(pt);
            const std::uint32_t need =
                sz <= 8 ? 1 : (sz <= 16 ? 2
                                        : static_cast<std::uint32_t>((sz + 7) / 8));
            if (need > 2) {
                // MEMORY class: baca semua unit dari stack caller.
                for (std::int32_t u = 0; u < static_cast<std::int32_t>(need); ++u) {
                    out_.ins("movq " + std::to_string(16 + (si + u) * 8) +
                             "(%rbp), %rax");
                    out_.ins("movq %rax, " + std::to_string(s->second - 8 * u) +
                             "(%rbp)");
                }
                si += need;
            } else if (reg + need <= 6) {
                if (const RegLoc* h = home(prm)) {
                    out_.ins(std::string("movq %") + kGp[reg] + ", %" + h->reg);
                } else {
                    out_.ins(std::string("movq %") + kGp[reg] + ", " +
                             std::to_string(s->second) + "(%rbp)");
                }
                if (sz > 8) {
                    out_.ins(std::string("movq %") + kGp[reg + 1] + ", " +
                             std::to_string(s->second - 8) + "(%rbp)");
                    reg += 2;
                } else {
                    ++reg;
                }
            } else if (sz <= 8) {
                // Overflow register -> stack arg ([rbp+16+8*si]).
                out_.ins("movq " + std::to_string(16 + si * 8) + "(%rbp), %rax");
                out_.ins("movq %rax, " + std::to_string(s->second) + "(%rbp)");
                ++si;
            } else if (pt != InvalidType && pt < module_.types.size() &&
                       module_.types[pt].kind == IrTypeKind::View) {
                out_.ins("movq " + std::to_string(16 + si * 8) + "(%rbp), %rax");
                out_.ins("movq %rax, " + std::to_string(s->second) + "(%rbp)");
                out_.ins("movq " + std::to_string(16 + (si + 1) * 8) + "(%rbp), %rax");
                out_.ins("movq %rax, " + std::to_string(s->second - 8) + "(%rbp)");
                si += 2;
            } else {
                fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                     "param tipe >8 byte non-View belum didukung");
                return false;
            }
        }
        return true;
    }

    // Konstanta float 16-byte di .rodata (.align 16 + pasangan .quad —
    // akses via movdqa, alignmen ketat 16-byte).
    std::string floatLabel(std::uint64_t bits) {
        const auto it = floatLabels_.find(bits);
        if (it != floatLabels_.end()) return it->second;
        const std::string lbl =
            ".LF" + std::to_string(floatLabels_.size()) + "_" + asmName(fn_.name);
        floatLabels_.emplace(bits, lbl);
        pendingFloats_->push_back({lbl, bits});
        return lbl;
    }

    // Nama simbol assembly: karakter selain [A-Za-z0-9_] -> '_'
    // (penting: .__global_init diawali titik = label LOKAL di GNU as).
    static std::string asmName(const std::string& s) {
        std::string out = s;
        for (char& c : out) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_';
            if (!ok) c = '_';
        }
        return out;
    }

    std::string globalLabel(std::uint32_t gid) const {
        return "rh_g_" + asmName(module_.globalVars[gid].name);
    }

    std::string blockLabel(BlockId b) {
        return ".LB" + std::to_string(b) + "_" + asmName(fn_.name);
    }

    // Helper tipe (pola identik PeepholeConstFold).
    bool typeSignedAt(TypeId t) const {
        return t != InvalidType && t < module_.types.size() &&
               module_.types[t].isSigned;
    }
    std::uint32_t typeSizeAt(TypeId t) const {
        return (t != InvalidType && t < module_.types.size())
                   ? module_.types[t].sizeBytes
                   : 0;
    }

    // Operand -> register. ValueV/ImmV lalu dipakai binop/cmp/call/ret.
    // Muat nilai float 16-byte dari slot ke xmm (movdqa, align 16 ketat).
    bool loadFloatSlot(const IrOperand& op, const std::string& reg) {
        if (op.kind != IrOperandKind::ValueV) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "float: operand bukan ValueV");
            return false;
        }
        const auto s = slotOfValue_.find(op.id);
        if (s == slotOfValue_.end()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "float: value tanpa slot");
            return false;
        }
        out_.ins("movdqa " + std::to_string(s->second) + "(%rbp), %" + reg);
        return true;
    }

    // Muat POINTER ke register: ValueV dari slot, atau GlobalV -> lea label.
    bool loadPtrToReg(const IrOperand& op, const std::string& reg) {
        if (op.kind == IrOperandKind::GlobalV) {
            if (op.id >= module_.globalVars.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "operand GlobalV invalid");
                return false;
            }
            out_.ins("leaq " + globalLabel(op.id) + "(%rip), %" + reg);
            return true;
        }
        return loadToReg(op, reg);
    }

    static bool isRegLoadable(const IrOperand& op) {
        return op.kind == IrOperandKind::ValueV ||
               op.kind == IrOperandKind::ImmV;
    }

    bool loadToReg(const IrOperand& op, const std::string& reg) {
        if (op.kind == IrOperandKind::ImmV) {
            out_.ins("movq $" + std::to_string(op.id) + ", %" + reg);
            return true;
        }
        if (op.kind == IrOperandKind::ValueV) {
            if (const RegLoc* h = home(op.id)) {
                out_.ins("movq %" + h->reg + ", %" + reg);
                return true;
            }
            const auto it = slotOfValue_.find(op.id);
            if (it == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "operand ValueV tanpa slot");
                return false;
            }
            out_.ins("movq " + std::to_string(it->second) + "(%rbp), %" + reg);
            return true;
        }
        fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
             "operand kind tidak bisa dimuat ke register: " +
                 std::to_string(static_cast<int>(op.kind)));
        return false;
    }

    bool storeReg(const std::string& reg, ValueId vid) {
        if (const RegLoc* h = home(vid)) {
            out_.ins("movq %" + reg + ", %" + h->reg);  // pulang ke register
            return true;
        }
        const auto it = slotOfValue_.find(vid);
        if (it == slotOfValue_.end()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "hasil tanpa slot");
            return false;
        }
        out_.ins("movq %" + reg + ", " + std::to_string(it->second) + "(%rbp)");
        return true;
    }

    // Konstanta string -> label .rodata unik per ConstId (cache).
    void pushString(const std::string& lbl, const IrConstant& c) {
        for (const PendingString& ps : *pendingStrings_) {
            if (ps.label == lbl) return;
        }
        pendingStrings_->push_back({lbl, c});
    }
    // Cetak string literal pendek via rh_print (kurung tuple dst).
    void emitPrintStr(const std::string& s) {
        IrConstant c;
        c.kind = IrConstantKind::String;
        c.length = s.size();
        c.bytes.assign(s.begin(), s.end());
        const std::string lbl = ".LCT" + std::to_string(tupleStrCounter_++) +
                                "_" + asmName(fn_.name);
        pushString(lbl, c);
        out_.ins("leaq " + lbl + "(%rip), %rdi");
        out_.ins("movq $" + std::to_string(s.size()) + ", %rsi");
        out_.ins("call rh_print");
    }
    // Cetak SATU field fat16 (8-byte di off(%rbp)) sesuai tipe field.
    // float: movq ke xmm0 (tak butuh align, beda movdqa slot utama).
    void emitFatField(std::int32_t off, TypeId t) {
        if (isFloat(module_, t)) {
            out_.ins("movq " + std::to_string(off) + "(%rbp), %xmm0");
            out_.ins("call rh_fprint");
            return;
        }
        out_.ins("movq " + std::to_string(off) + "(%rbp), %rdi");
        out_.ins(std::string("call ") + printSymForType(module_, t));
    }
    std::string stringLabel(ConstId cid, std::uint32_t instrIdx) {
        const auto it = stringLabels_.find(cid);
        if (it != stringLabels_.end()) return it->second;
        const std::string lbl = ".LC" + std::to_string(instrIdx) + "_" + asmName(fn_.name);
        stringLabels_.emplace(cid, lbl);
        return lbl;
    }

    bool resultValue(std::uint32_t instrIdx, ValueId& out) const {
        const auto it = valueOfInstr_.find(instrIdx);
        if (it == valueOfInstr_.end()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "instruksi tanpa value hasil");
            return false;
        }
        out = it->second;
        return true;
    }

    // ---- per-block emission ----
    bool emitBlock(std::uint32_t b) {
        const IrBlock& block = fn_.blocks[b];
        out_.label(blockLabel(static_cast<BlockId>(b)));

        for (std::uint32_t k = 0; k < block.insts.size(); ++k) {
            const InstrId iid = block.insts[k];
            if (iid >= fn_.instrs.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "InstrId invalid");
                return false;
            }
            if (!emitInstr(fn_.instrs[iid], iid, b)) {
                return false;
            }
        }
        return true;
    }

    // Phi moves untuk edge (pred ordinal -> succ block), dipanggil tepat
    // SEBELUM jump terminator di blok pred.
    bool emitPhiMoves(std::uint32_t predOrdinal, BlockId succId) {
        if (succId >= fn_.blocks.size()) return true;
        moves_.clear();  // per-edge fresh
        const IrBlock& succ = fn_.blocks[succId];
        for (const InstrId iid : succ.insts) {
            if (iid >= fn_.instrs.size() || fn_.instrs[iid].op != IrOpcode::Phi) {
                continue;
            }
            const IrInstr& phi = fn_.instrs[iid];
            ValueId phiValue = 0;
            if (!resultValue(iid, phiValue)) return false;
            // Pasangan (BlockV pred, ValueV incoming).
            for (std::size_t k = 0; k + 1 < phi.operands.size(); k += 2) {
                if (phi.operands[k].id != predOrdinal) {
                    continue;
                }
                const IrOperand& incoming = phi.operands[k + 1];
                const auto islot = slotOfValue_.find(incoming.id);
                // Register home juga valid (bukan hanya slot).
                if (incoming.kind != IrOperandKind::ValueV ||
                    (islot == slotOfValue_.end() &&
                     home(incoming.id) == nullptr)) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "phi: incoming tanpa slot/reg");
                    return false;
                }
                const auto pslot = slotOfValue_.find(phiValue);
                if (pslot == slotOfValue_.end() && home(phiValue) == nullptr) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "phi: hasil tanpa slot/reg");
                    return false;
                }

                const std::uint32_t units = slotUnitsOf(fn_.values[phiValue].type);
                const RegLoc* srcH = home(incoming.id);
                const RegLoc* dstH = home(phiValue);
                if (units > 1) {
                    // Agregat: bukan register-home, sequential aman.
                    for (std::int32_t u = 0; u < static_cast<std::int32_t>(units); ++u) {
                        out_.ins("movq " + std::to_string(islot->second - 8 * u) +
                                 "(%rbp), %rax");
                        out_.ins("movq %rax, " +
                                 std::to_string(pslot->second - 8 * u) + "(%rbp)");
                    }
                    continue;
                }
                // Kumpulkan move (src -> dst) untuk algoritma parallel-move.
                PMove mv;
                mv.src = srcH ? ("%" + srcH->reg)
                              : (std::to_string(islot->second) + "(%rbp)");
                mv.dst = dstH ? ("%" + dstH->reg)
                              : (std::to_string(pslot->second) + "(%rbp)");
                moves_.push_back(mv);
            }
        }
        // Flush SEKALI per pred: SEMUA phi succ untuk pred ini dalam SATU
        // batch agar algoritma parallel-move (incl. siklus-break %r10)
        // menangani konflik ANTAR-PHI. Flush per-phi membuat mov phi ke-2
        // membaca register yang baru ditimpa mov phi ke-1 (bug: init
        // hitung=1 bukan 0 di bilangan_prima — Total 26).
        if (!emitParallelMoves()) return false;
        moves_.clear();
        return true;
    }

    struct PMove { std::string src; std::string dst; };

    // Satu move: mem->mem wajib lewat %rax (x86 tak punya mov mem,mem);
    // yang melibatkan register langsung valid.
    void emitOneMove(const PMove& mv) {
        const bool srcMem = mv.src.rfind('%', 0) != 0;
        const bool dstMem = mv.dst.rfind('%', 0) != 0;
        if (srcMem && dstMem) {
            out_.ins("movq " + mv.src + ", %rax");
            out_.ins("movq %rax, " + mv.dst);
        } else {
            out_.ins("movq " + mv.src + ", " + mv.dst);
        }
    }

    // Parallel-move dengan satu scratch (r10): pindahkan yang aman dulu
    // (src bukan dst move lain); bila tak ada (siklus), simpan src pertama
    // ke r10, tunda dst-nya, lanjutkan; flush tundaan saat graf habis atau
    // siklus baru muncul.
    bool emitParallelMoves() {
        auto srcIsDstOfOther = [&](std::size_t idx) {
            for (std::size_t j = 0; j < moves_.size(); ++j) {
                if (j != idx && moves_[j].dst == moves_[idx].src &&
                    moves_[j].dst.rfind('%', 0) == 0) {
                    return true;
                }
            }
            return false;
        };
        // KRITIS: move idx harus DITUNDA bila dst-nya adalah SRC move lain
        // yang belum dieksekusi — mengeksekusi idx lebih dulu akan MENIMPA
        // sumber move lain (bug: phi moves entry -> header, init hitung=1
        // bukan 0 di bilangan_prima: %v5->%i menimpa %r12, src %v4->%hitung).
        auto dstIsSrcOfOther = [&](std::size_t idx) {
            for (std::size_t j = 0; j < moves_.size(); ++j) {
                if (j != idx && moves_[j].src == moves_[idx].dst) {
                    return true;
                }
            }
            return false;
        };
        std::vector<std::string> deferred;
        while (!moves_.empty()) {
            bool any = true;
            while (any) {
                any = false;
                for (std::size_t i = 0; i < moves_.size(); ++i) {
                    if (!srcIsDstOfOther(i) && !dstIsSrcOfOther(i)) {
                        emitOneMove(moves_[i]);
                        moves_.erase(moves_.begin() + i);
                        any = true;
                        break;
                    }
                }
            }
            if (moves_.empty()) break;
            // Siklus: flush tundaan dulu (r10 tak boleh ditimpa), lalu
            // putus siklus dengan menyimpan src move-0 ke r10.
            for (const std::string& d : deferred) {
                out_.ins("movq %r10, " + d);
            }
            deferred.clear();
            out_.ins("movq " + moves_[0].src + ", %r10");
            deferred.push_back(moves_[0].dst);
            moves_.erase(moves_.begin());
        }
        for (const std::string& d : deferred) {
            out_.ins("movq %r10, " + d);
        }
        return true;
    }


    bool emitInstr(const IrInstr& inst, std::uint32_t instrIdx,
                   std::uint32_t blockOrdinal) {
        ValueId result = 0;
        const bool hasResult = valueOfInstr_.count(instrIdx) != 0;

        switch (inst.op) {
        case IrOpcode::Phi:
            return true;  // nilai diisi oleh edge moves
        case IrOpcode::ConstInt: {
            std::uint64_t value = 0;
            if (!decodeConstInt(inst, value)) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "const_int: operand ImmV invalid");
                return false;
            }
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("movq $" + std::to_string(value) + ", %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::ConstBool: {
            if (inst.operands.empty() || inst.operands[0].kind != IrOperandKind::ImmV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "const_bool: operand ImmV invalid");
                return false;
            }
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("movq $" + std::to_string(inst.operands[0].id) + ", %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::ConstString: {
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::ConstantV ||
                inst.operands[0].id >= module_.constants.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "const_string: operand ConstantV invalid");
                return false;
            }
            const IrConstant& c = module_.constants[inst.operands[0].id];
            const std::string lbl = stringLabel(inst.operands[0].id, instrIdx);
            pushString(lbl, c);
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("leaq " + lbl + "(%rip), %rax");
            if (!storeReg("rax", result)) return false;
            if (typeSizeAt(inst.resultType) > 8) {
                // View {ptr,len}: len di slot kedua.
                const auto s = slotOfValue_.find(result);
                out_.ins("movq $" + std::to_string(c.bytes.size()) + ", %rax");
                out_.ins("movq %rax, " + std::to_string(s->second - 8) + "(%rbp)");
            }
            return true;
        }
        case IrOpcode::Load:
        case IrOpcode::Deref: {
            // load ptr / deref: movq (%r), value. Operand ValueV (pointer)
            // atau GlobalV (variabel global langsung, spesifikasi irVersion 3).
            if (inst.operands.empty() ||
                !loadPtrToReg(inst.operands[0], "rax")) {
                return false;
            }
            out_.ins("movq (%rax), %rcx");
            if (!resultValue(instrIdx, result)) return false;
            if (!storeReg("rcx", result)) return false;
            const std::uint64_t lsz = typeSizeAt(inst.resultType);
            if (lsz > 8) {
                // Agregat (View/struct/larik): kata ke-w heap di +8*w
                // ke slot kedua dst (slot menurun: kata ke-w di slot-8*w).
                const auto s = slotOfValue_.find(result);
                if (s != slotOfValue_.end()) {
                    const std::uint32_t nw =
                        static_cast<std::uint32_t>(lsz / 8);
                    for (std::uint32_t w = 1; w < nw; ++w) {
                        out_.ins("movq " +
                                 std::to_string(8 * static_cast<int>(w)) +
                                 "(%rax), %rcx");
                        out_.ins("movq %rcx, " +
                                 std::to_string(static_cast<int>(s->second) -
                                                8 * static_cast<int>(w)) +
                                 "(%rbp)");
                    }
                }
            }
            return true;
        }
        case IrOpcode::Store: {
            // store ptr val: movq val, (%r). ptr boleh ValueV atau GlobalV.
            if (inst.operands.size() < 2 ||
                !isRegLoadable(inst.operands[1])) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "store: operand value invalid");
                return false;
            }
            if (!loadToReg(inst.operands[1], "rax")) return false;  // val
            if (!loadPtrToReg(inst.operands[0], "rcx")) return false;  // ptr
            out_.ins("movq %rax, (%rcx)");
            if (inst.operands[1].kind == IrOperandKind::ValueV &&
                inst.operands[1].id < fn_.values.size() &&
                typeSizeAt(fn_.values[inst.operands[1].id].type) > 8) {
                // Agregat: kata ke-w dari slot (slot-8*w) ke heap (+8*w).
            const auto s = slotOfValue_.find(inst.operands[1].id);
            if (s != slotOfValue_.end()) {
                const std::uint32_t nw =
                    static_cast<std::uint32_t>(typeSizeAt(
                        fn_.values[inst.operands[1].id].type) / 8);
                for (std::uint32_t w = 1; w < nw; ++w) {
                    out_.ins("movq " +
                             std::to_string(static_cast<int>(s->second) -
                                            8 * static_cast<int>(w)) +
                             "(%rbp), %rax");
                    out_.ins("movq %rax, " +
                             std::to_string(8 * static_cast<int>(w)) +
                             "(%rcx)");
                }
            }
            }
            return true;
        }
        case IrOpcode::FieldAddr: {
            // lea off(%ptr): [0]ptr [1]ImmV offset
            if (inst.operands.size() < 2 ||
                inst.operands[0].kind != IrOperandKind::ValueV ||
                inst.operands[1].kind != IrOperandKind::ImmV ||
                !loadToReg(inst.operands[0], "rax")) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "field_addr: operand invalid");
                return false;
            }
            const std::int32_t off =
                static_cast<std::int32_t>(inst.operands[1].id);
            out_.ins("leaq " + std::to_string(off) + "(%rax), %rax");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::AddrOf: {
            // addr_of global: [0]GlobalV -> lea simbol global.
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::GlobalV ||
                inst.operands[0].id >= module_.globalVars.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "addr_of: operand GlobalV invalid");
                return false;
            }
            out_.ins("leaq " + globalLabel(inst.operands[0].id) + "(%rip), %rax");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::Len: {
            // Array inline: panjang = arraySize konstan. Pointer hasil
            // Alloc: header len di -8(ptr) (konvensi heap RhBuiltin).
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::ValueV ||
                inst.operands[0].id >= fn_.values.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "len: operand ValueV invalid");
                return false;
            }
            const TypeId lt = fn_.values[inst.operands[0].id].type;
            if (!resultValue(instrIdx, result)) return false;
            if (lt != InvalidType && lt < module_.types.size() &&
                module_.types[lt].kind == IrTypeKind::Array) {
                out_.ins("movq $" +
                         std::to_string(module_.types[lt].arraySize) + ", %rax");
                return storeReg("rax", result);
            }
            if (!loadToReg(inst.operands[0], "rax")) return false;
            out_.ins("movq -8(%rax), %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::And:
        case IrOpcode::Or: {
            // Operand bool 0/1 -> bitwise = logika.
            if (inst.operands.size() < 2 ||
                !loadToReg(inst.operands[0], "rax") ||
                !loadToReg(inst.operands[1], "rcx")) {
                return false;
            }
            out_.ins(inst.op == IrOpcode::And ? "andq %rcx, %rax"
                                              : "orq %rcx, %rax");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::Pow: {
            // call rh_pow(base, exp)
            if (inst.operands.size() < 2 ||
                !loadToReg(inst.operands[0], "rdi") ||
                !loadToReg(inst.operands[1], "rsi")) {
                return false;
            }
            out_.ins("xorl %eax, %eax");
            out_.ins("call rh_pow");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::Construct: {
            // construct @T v0..vn -> susun field/elemen di slot hasil.
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::TypeV ||
                inst.operands[0].id >= module_.types.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "construct: operand TypeV invalid");
                return false;
            }
            const IrType& ct = module_.types[inst.operands[0].id];
            if (!resultValue(instrIdx, result)) return false;
            const auto s = slotOfValue_.find(result);
            std::uint32_t fi = 0;
            for (std::size_t k = 1; k < inst.operands.size(); ++k, ++fi) {
                if (!loadToReg(inst.operands[k], "rax")) return false;
                std::int32_t delta = 0;
                if (ct.kind == IrTypeKind::Struct && fi < ct.fields.size()) {
                    delta = -static_cast<std::int32_t>(ct.fields[fi].offsetBytes);
                } else {
                    const std::uint32_t esz =
                        ct.elementType != InvalidType &&
                                ct.elementType < module_.types.size()
                            ? module_.types[ct.elementType].sizeBytes
                            : 8;
                    delta = -static_cast<std::int32_t>(fi * esz);
                }
                // mov SESUAI ukuran field/elemen — movq 8-byte utk
                // esz<8 menimpa tetangga (construct array karakter:
                // hanya elemen terakhir selamat, sisanya tertimpa).
                std::uint32_t fsz = 8;
                if (ct.kind == IrTypeKind::Struct && fi < ct.fields.size()) {
                    fsz = ct.fields[fi].type < module_.types.size()
                              ? module_.types[ct.fields[fi].type].sizeBytes
                              : 8;
                } else if (ct.elementType != InvalidType &&
                           ct.elementType < module_.types.size()) {
                    fsz = module_.types[ct.elementType].sizeBytes;
                }
                const std::string dst =
                    std::to_string(s->second + delta) + "(%rbp)";
                switch (fsz) {
                    case 1: out_.ins("movb %al, " + dst); break;
                    case 2: out_.ins("movw %ax, " + dst); break;
                    case 4: out_.ins("movl %eax, " + dst); break;
                    default: out_.ins("movq %rax, " + dst); break;
                }
            }
            return true;
        }
        case IrOpcode::Extract: {
            // extract arr idx: muat elemen (ukuran <=8) dari slot arr.
            if (inst.operands.size() < 2 ||
                inst.operands[0].kind != IrOperandKind::ValueV ||
                !isRegLoadable(inst.operands[1]) ||
                !loadToReg(inst.operands[1], "rax")) {  // idx
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "extract: operand invalid");
                return false;
            }
            const TypeId at = fn_.values[inst.operands[0].id].type;
            std::uint32_t esz = 8;
            if (at != InvalidType && at < module_.types.size() &&
                module_.types[at].elementType != InvalidType &&
                module_.types[at].elementType < module_.types.size()) {
                esz = module_.types[module_.types[at].elementType].sizeBytes;
            }
            // arr mode: SLOT (stack agregat, slot menurun) atau HEAP
            // POINTER (alloc pointer di register — offset MENAIK).
            const auto s = slotOfValue_.find(inst.operands[0].id);
            const RegLoc* h = (s == slotOfValue_.end())
                                  ? home(inst.operands[0].id)
                                  : nullptr;
            if (s == slotOfValue_.end() && !h) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "extract: arr tanpa slot/home");
                return false;
            }
            out_.ins("imulq $" + std::to_string(esz) + ", %rax");
            if (s != slotOfValue_.end()) {
                // stack: alamat = rbp + slotOff - idx*esz (slot menurun)
                out_.ins("negq %rax");
                out_.ins("leaq " + std::to_string(s->second) + "(%rbp), %rcx");
                out_.ins("addq %rax, %rcx");
            } else {
                // heap: alamat = ptr + idx*esz (heap menaik)
                out_.ins("movq %" + h->reg + ", %rcx");
                out_.ins("addq %rax, %rcx");
            }
            if (!resultValue(instrIdx, result)) return false;
            if (esz > 8) {
                // Elemen agregat (larik nested/struct kecil): copy esz/8
                // kata — kata ke-w elemen di -8*w dari alamat elem
                // (konvensi: kata ke-0 di alamat, kata ke-1 di -8, ...).
                // Hasil agregat >8 byte SELALU punya slot (tak eligible
                // register home).
                const auto ds = slotOfValue_.find(result);
                if (ds == slotOfValue_.end()) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "extract: hasil agregat tanpa slot");
                    return false;
                }
                const std::uint32_t nw = esz / 8;
                const bool heapArr = (s == slotOfValue_.end());
                for (std::uint32_t w = 0; w < nw; ++w) {
                    const std::string eo = heapArr
                        ? (w == 0 ? "" : std::to_string(8 * static_cast<int>(w)))
                        : (w == 0 ? "" : std::to_string(-8 * static_cast<int>(w)));
                    out_.ins("movq " + eo + "(%rcx), %rax");
                    out_.ins("movq %rax, " +
                             std::to_string(static_cast<int>(ds->second) -
                                            8 * static_cast<int>(w)) +
                             "(%rbp)");
                }
                return true;
            }
            out_.ins("movq (%rcx), %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::Insert: {
            // insert arr idx val: salin seluruh nilai (n slot) ke hasil,
            // lalu tulis elemen baru.
            if (inst.operands.size() < 3 ||
                inst.operands[0].kind != IrOperandKind::ValueV ||
                !isRegLoadable(inst.operands[1]) ||
                !isRegLoadable(inst.operands[2])) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "insert: operand invalid");
                return false;
            }
            const TypeId at = fn_.values[inst.operands[0].id].type;
            if (at == InvalidType || at >= module_.types.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                     "insert: tipe invalid");
                return false;
            }
            if (!resultValue(instrIdx, result)) return false;
            const auto src = slotOfValue_.find(inst.operands[0].id);
            const RegLoc* srcH =
                (src == slotOfValue_.end()) ? home(inst.operands[0].id)
                                            : nullptr;
            if (src == slotOfValue_.end() && !srcH) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "insert: src tanpa slot/home");
                return false;
            }
            // NOTE: hasil insert DI-HOME-KAN (chained value, contoh
            // ptr_load.rhc) -> slot dst tak ada. Konvensi home value =
            // kata-0 di register, kata ke-u>0 di scratch -8u-16 — BISA
            // MENIMPA SLOT VALUE LAIN -> segfault. SEMENTARA: gagal
            // eksplisit; fix konsumen home-value (register+scratch di
            // atas frame) = utang terpisah.
            const auto dst = slotOfValue_.find(result);
            if (dst == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "insert: hasil di-home-kan (belum didukung)");
                return false;
            }
            const std::uint32_t units = slotUnitsOf(at);
            // src: SLOT (stack, -8u) / HEAP PTR (register, +8u menaik)
            const bool heapSrc = (src == slotOfValue_.end());
            for (std::int32_t u = 0; u < static_cast<std::int32_t>(units); ++u) {
                if (heapSrc) {
                    const std::string so = u == 0
                        ? "" : std::to_string(8 * static_cast<int>(u));
                    out_.ins("movq " + so + "(%" + srcH->reg + "), %rax");
                } else {
                    out_.ins("movq " +
                             std::to_string(src->second - 8 * u) + "(%rbp), %rax");
                }
                out_.ins("movq %rax, " +
                         std::to_string(dst->second - 8 * u) + "(%rbp)");
            }
            // tulis elemen
            std::uint32_t esz = 8;
            if (at < module_.types.size() &&
                module_.types[at].elementType != InvalidType &&
                module_.types[at].elementType < module_.types.size()) {
                esz = module_.types[module_.types[at].elementType].sizeBytes;
            }
            if (!loadToReg(inst.operands[1], "rcx")) return false;  // idx
            out_.ins("imulq $" + std::to_string(esz) + ", %rcx");
            out_.ins("negq %rcx");
            out_.ins("leaq " + std::to_string(dst->second) + "(%rbp), %rdx");
            out_.ins("addq %rcx, %rdx");
            if (esz > 8) {
                // src agregat: SLOT (stack, slot menurun -8w) atau HEAP
                // POINTER (register home — layout MENAIK +8w).
                const auto vs = slotOfValue_.find(inst.operands[2].id);
                const RegLoc* vh = (vs == slotOfValue_.end())
                                       ? home(inst.operands[2].id)
                                       : nullptr;
                if (vs == slotOfValue_.end() && !vh) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "insert: val agregat tanpa slot/home");
                    return false;
                }
                const std::uint32_t nw = esz / 8;
                const bool heapVal = (vs == slotOfValue_.end());
                for (std::uint32_t w = 0; w < nw; ++w) {
                    const std::string eo =
                        w == 0 ? "" : std::to_string(-8 * static_cast<int>(w));
                    if (heapVal) {
                        const std::string so = w == 0
                            ? "" : std::to_string(8 * static_cast<int>(w));
                        out_.ins("movq " + so + "(%" + vh->reg + "), %rax");
                    } else {
                        out_.ins("movq " +
                                 std::to_string(static_cast<int>(vs->second) -
                                                8 * static_cast<int>(w)) +
                                 "(%rbp), %rax");
                    }
                    out_.ins("movq %rax, " + eo + "(%rdx)");
                }
                return true;
            }
            if (!loadToReg(inst.operands[2], "rax")) return false;  // val
            // mov SESUAI ukuran elemen — movq 8-byte untuk esz<8
            // menimpa elemen tetangga (store karakter ke array:
            // teks1[0..2] tertimpa, hanya terakhir selamat).
            switch (esz) {
                case 1: out_.ins("movb %al, (%rdx)"); break;
                case 2: out_.ins("movw %ax, (%rdx)"); break;
                case 4: out_.ins("movl %eax, (%rdx)"); break;
                default: out_.ins("movq %rax, (%rdx)"); break;
            }
            return true;
        }
        case IrOpcode::CopyConst: {
            // copy_const ptr @C: salin blob konstanta ke area ptr (rep movsb).
            if (inst.operands.size() < 2 ||
                inst.operands[0].kind != IrOperandKind::ValueV ||
                inst.operands[1].kind != IrOperandKind::ConstantV ||
                inst.operands[1].id >= module_.constants.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "copy_const: operand invalid");
                return false;
            }
            const IrConstant& c = module_.constants[inst.operands[1].id];
            const std::string lbl = stringLabel(inst.operands[1].id, instrIdx);
            pushString(lbl, c);
            if (!loadToReg(inst.operands[0], "rdi")) return false;
            out_.ins("leaq " + lbl + "(%rip), %rsi");
            out_.ins("movq $" + std::to_string(c.bytes.size()) + ", %rcx");
            out_.ins("rep movsb");
            return true;
        }
        case IrOpcode::Inc:
        case IrOpcode::Dec:
        case IrOpcode::Neg:
        case IrOpcode::BitNot: {
            if (inst.operands.empty() || !loadToReg(inst.operands[0], "rax")) {
                return false;
            }
            if (isFloat(module_, inst.resultType)) {
                if (inst.op == IrOpcode::Neg) {
                    // -f = xorpd mask tanda 16-byte (kedua lane).
                    out_.ins("movdqa " + floatLabel(0x8000000000000000ULL) +
                             "(%rip), %xmm1");
                    out_.ins("movq %rax, %xmm0");
                    out_.ins("xorpd %xmm1, %xmm0");
                } else if (inst.op == IrOpcode::Inc ||
                           inst.op == IrOpcode::Dec) {
                    // ++/-- float = +/- 1.0 (SSE addsd/subsd); bit
                    // pattern 1.0 = 0x3FF0000000000000.
                    out_.ins("movq %rax, %xmm0");
                    out_.ins(std::string(inst.op == IrOpcode::Inc
                                             ? "addsd "
                                             : "subsd ") +
                             floatLabel(0x3FF0000000000000ULL) + "(%rip), %xmm0");
                } else {
                    fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                         "op bitwise tak berlaku untuk float");
                    return false;
                }
                if (!resultValue(instrIdx, result)) return false;
                out_.ins("movdqa %xmm0, " +
                         std::to_string(slotOfValue_.at(result)) + "(%rbp)");
                return true;
            } else if (inst.op == IrOpcode::Inc) out_.ins("incq %rax");
            else if (inst.op == IrOpcode::Dec) out_.ins("decq %rax");
            else if (inst.op == IrOpcode::Neg) out_.ins("negq %rax");
            else out_.ins("notq %rax");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::Not: {
            if (inst.operands.empty() || !loadToReg(inst.operands[0], "rax")) {
                return false;
            }
            out_.ins("testq %rax, %rax");
            out_.ins("sete %al");
            out_.ins("movzbq %al, %rax");
            if (!resultValue(instrIdx, result)) return false;
            return storeReg("rax", result);
        }
        case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul:
        case IrOpcode::BitAnd: case IrOpcode::BitOr: case IrOpcode::BitXor:
        case IrOpcode::Div: case IrOpcode::DivFloor: case IrOpcode::Mod:
        case IrOpcode::Shl: case IrOpcode::Shr:
            return emitBinop(inst, instrIdx);
        case IrOpcode::CmpEq: case IrOpcode::CmpNe:
        case IrOpcode::CmpLt: case IrOpcode::CmpLe:
        case IrOpcode::CmpGt: case IrOpcode::CmpGe:
            return emitCmp(inst, instrIdx);
        case IrOpcode::Cast:
            return emitCast(inst, instrIdx);
        case IrOpcode::Call:
        case IrOpcode::CallIndirect:
            return emitCall(inst, instrIdx);
        case IrOpcode::ConstFloat: {
            // Bit IEEE-64 f64 sebagai DUA ImmV (lo, hi) — pola serupa
            // ConstInt (D-4). Nilai float di slot = bit 64 (semua
            // pemindahan = salin bit; hanya operasi float yang pakai xmm).
            if (inst.operands.size() < 2 ||
                inst.operands[0].kind != IrOperandKind::ImmV ||
                inst.operands[1].kind != IrOperandKind::ImmV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "const_float: operand (ImmV lo, ImmV hi) invalid");
                return false;
            }
            const std::uint64_t bits =
                static_cast<std::uint64_t>(inst.operands[0].id) |
                (static_cast<std::uint64_t>(inst.operands[1].id) << 32);
            if (!resultValue(instrIdx, result)) return false;
            const std::string lbl = floatLabel(bits);
            out_.ins("movdqa " + lbl + "(%rip), %xmm0");
            out_.ins("movdqa %xmm0, " +
                     std::to_string(slotOfValue_.at(result)) + "(%rbp)");
            return true;
        }
        case IrOpcode::ConstNull: {
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("xorq %rax, %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::Alloc: {
            // Area frame statis dengan layout heap RhBuiltin:
            // [quad: len bytes data][data ...]; nilai = alamat data,
            // sehingga Len = load -8(ptr) dan kompatibel dg rh_alloc.
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::TypeV ||
                inst.operands[0].id >= module_.types.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "alloc: operand TypeV invalid");
                return false;
            }
            const TypeId at = inst.operands[0].id;
            const std::uint64_t sz = typeSizeAt(at);
            allocUsed_ = (allocUsed_ + 15) / 16 * 16;
            const std::int32_t lenOff =
                -(static_cast<std::int32_t>(slotCount_) * 8 +
                  static_cast<std::int32_t>(allocUsed_) + 8 +
                  static_cast<std::int32_t>(shift_));
            allocUsed_ += 8 + static_cast<std::uint32_t>(sz);
            if (!resultValue(instrIdx, result)) return false;
            out_.ins("movq $" + std::to_string(sz) + ", %rax");
            out_.ins("movq %rax, " + std::to_string(lenOff) + "(%rbp)");
            out_.ins("leaq " + std::to_string(lenOff + 8) + "(%rbp), %rax");
            return storeReg("rax", result);
        }
        case IrOpcode::Br: {
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::BlockV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "br: target BlockV invalid");
                return false;
            }
            if (inst.operands[0].id >= fn_.blocks.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "br: block target tak dikenal");
                return false;
            }
            if (!emitPhiMoves(blockOrdinal, inst.operands[0].id)) return false;
            out_.ins("jmp " + blockLabel(inst.operands[0].id));
            return true;
        }
        case IrOpcode::CondBr: {
            if (inst.operands.size() < 3) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "cond_br: operand kurang");
                return false;
            }
            if (!loadToReg(inst.operands[0], "rax")) return false;
            if (inst.operands[1].id >= fn_.blocks.size() ||
                inst.operands[2].id >= fn_.blocks.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "cond_br: block target tak dikenal");
                return false;
            }
            const std::string elseLocal = ".LCBr" + std::to_string(instrIdx) + "_" + fn_.name;
            out_.ins("testq %rax, %rax");
            out_.ins("je " + elseLocal);
            if (!emitPhiMoves(blockOrdinal, inst.operands[1].id)) return false;
            out_.ins("jmp " + blockLabel(inst.operands[1].id));
            out_.label(elseLocal);
            if (!emitPhiMoves(blockOrdinal, inst.operands[2].id)) return false;
            out_.ins("jmp " + blockLabel(inst.operands[2].id));
            return true;
        }
        case IrOpcode::SwitchInt:
            return emitSwitchInt(inst, instrIdx, blockOrdinal);
        case IrOpcode::RetVoid:
            emitEpilogue();
            return true;
        case IrOpcode::Ret: {
            if (!inst.operands.empty()) {
                if (inst.operands[0].kind == IrOperandKind::ValueV &&
                    inst.operands[0].id < fn_.values.size() &&
                    typeSizeAt(fn_.values[inst.operands[0].id].type) > 8) {
                    fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                         "ret value >8 byte belum didukung");
                    return false;
                }
                if (!loadToReg(inst.operands[0], "rax")) {
                    return false;
                }
                if (isFloat(module_, fn_.values[inst.operands[0].id].type)) {
                    const auto fs = slotOfValue_.find(inst.operands[0].id);
                    out_.ins("movdqa " + std::to_string(fs->second) +
                             "(%rbp), %xmm0");  // return f64 di xmm0
                }
            }
            emitEpilogue();
            return true;
        }
        case IrOpcode::Append: {
            // a +> b (teks View): rh_concat(a_ptr, a_len, b_ptr, b_len)
            // -> View{buf, len} di rax:rdx -> slot & slot-8 (konvensi fat16).
            if (inst.operands.size() < 2 ||
                inst.operands[0].kind != IrOperandKind::ValueV ||
                inst.operands[1].kind != IrOperandKind::ValueV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "append: operand bukan ValueV");
                return false;
            }
            const auto sa = slotOfValue_.find(inst.operands[0].id);
            const auto sb = slotOfValue_.find(inst.operands[1].id);
            if (sa == slotOfValue_.end() || sb == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "append: operand tanpa slot");
                return false;
            }
            out_.ins("movq " + std::to_string(sa->second) + "(%rbp), %rdi");
            out_.ins("movq " + std::to_string(sa->second - 8) + "(%rbp), %rsi");
            out_.ins("movq " + std::to_string(sb->second) + "(%rbp), %rdx");
            out_.ins("movq " + std::to_string(sb->second - 8) + "(%rbp), %rcx");
            out_.ins("call rh_concat");
            if (!resultValue(instrIdx, result)) return false;
            const auto s = slotOfValue_.find(result);
            if (s == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "append: hasil tanpa slot");
                return false;
            }
            out_.ins("movq %rax, " + std::to_string(s->second) + "(%rbp)");
            out_.ins("movq %rdx, " +
                     std::to_string(s->second - 8) + "(%rbp)");
            return true;
        }
        default:
            fail(errors_, RhBackendErrorCode::UnsupportedOpcode, fn_.name,
                 "opcode belum didukung Fase 2: " +
                     std::to_string(static_cast<int>(inst.op)));
            return false;
        }
        (void)hasResult;
    }

    bool emitBinop(const IrInstr& inst, std::uint32_t instrIdx) {
        // Operand boleh ValueV (slot) atau ImmV (konstanta hasil const-prop
        // optimizer) — loadToReg menangani keduanya.
        if (inst.operands.size() < 2 ||
            !isRegLoadable(inst.operands[0]) ||
            !isRegLoadable(inst.operands[1])) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "binop: operand bukan ValueV/ImmV");
            return false;
        }
        if (isFloat(module_, inst.resultType)) {
            // Aritmetika f64: bit slot -> xmm, sd-op, kembali ke slot.
            switch (inst.op) {
            case IrOpcode::Add: case IrOpcode::Sub: case IrOpcode::Mul:
            case IrOpcode::Div:
                break;
            default:
                fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                     "binop float: operator belum didukung");
                return false;
            }
            if (!loadFloatSlot(inst.operands[0], "xmm0")) return false;
            if (!loadFloatSlot(inst.operands[1], "xmm1")) return false;
            switch (inst.op) {
            case IrOpcode::Add: out_.ins("addpd %xmm1, %xmm0"); break;
            case IrOpcode::Sub: out_.ins("subpd %xmm1, %xmm0"); break;
            case IrOpcode::Mul: out_.ins("mulpd %xmm1, %xmm0"); break;
            case IrOpcode::Div: out_.ins("divpd %xmm1, %xmm0"); break;
            default: break;
            }
            ValueId fr = 0;
            if (!resultValue(instrIdx, fr)) return false;
            out_.ins("movdqa %xmm0, " + std::to_string(slotOfValue_.at(fr)) + "(%rbp)");
            return true;
        }
        if (!loadToReg(inst.operands[0], "rax")) return false;
        if (inst.op == IrOpcode::Shl || inst.op == IrOpcode::Shr) {
            if (!loadToReg(inst.operands[1], "rcx")) return false;
            const bool sgn = inst.resultType < module_.types.size() &&
                             typeSignedAt(inst.resultType);
            out_.ins(inst.op == IrOpcode::Shl ? "shlq %cl, %rax"
                                              : (sgn ? "sarq %cl, %rax"
                                                     : "shrq %cl, %rax"));
        } else if (inst.op == IrOpcode::Div || inst.op == IrOpcode::DivFloor ||
                   inst.op == IrOpcode::Mod) {
            if (!loadToReg(inst.operands[1], "rcx")) return false;
            out_.ins("cqo");
            out_.ins("idivq %rcx");  // rax = quo, rdx = rem
            if (inst.op != IrOpcode::Div) {
                // Koreksi floor division (DEVIATIONS D-1): bila sisa != 0
                // dan tanda pembagi berbeda, sesuaikan hasil.
                const std::string done = ".LFlr" + std::to_string(instrIdx) + "_" + fn_.name;
                out_.ins("testq %rdx, %rdx");
                out_.ins("je " + done);
                out_.ins("movq %rax, %rsi");
                out_.ins("xorq %rcx, %rsi");   // tanda quo vs divisor
                out_.ins("jns " + done);
                if (inst.op == IrOpcode::DivFloor) {
                    out_.ins("decq %rax");
                } else {
                    out_.ins("addq %rcx, %rdx");  // floor mod
                }
                out_.label(done);
            }
            if (inst.op == IrOpcode::Mod) {
                out_.ins("movq %rdx, %rax");
            }
        } else {
            if (!loadToReg(inst.operands[1], "rcx")) return false;
            switch (inst.op) {
            case IrOpcode::Add:     out_.ins("addq %rcx, %rax"); break;
            case IrOpcode::Sub:     out_.ins("subq %rcx, %rax"); break;
            case IrOpcode::Mul:     out_.ins("imulq %rcx, %rax"); break;
            case IrOpcode::BitAnd:  out_.ins("andq %rcx, %rax"); break;
            case IrOpcode::BitOr:   out_.ins("orq %rcx, %rax"); break;
            case IrOpcode::BitXor:  out_.ins("xorq %rcx, %rax"); break;
            default: break;
            }
        }
        ValueId result = 0;
        if (!resultValue(instrIdx, result)) return false;
        return storeReg("rax", result);
    }

    bool emitCmp(const IrInstr& inst, std::uint32_t instrIdx) {
        if (inst.operands.size() < 2 ||
            !isRegLoadable(inst.operands[0]) ||
            !isRegLoadable(inst.operands[1])) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "cmp: operand bukan ValueV/ImmV");
            return false;
        }
        const bool op0Float =
            inst.operands[0].kind == IrOperandKind::ValueV &&
            inst.operands[0].id < fn_.values.size() &&
            isFloat(module_, fn_.values[inst.operands[0].id].type);
        if (op0Float) {
            if (!loadToReg(inst.operands[0], "rax")) return false;
            if (!loadToReg(inst.operands[1], "rcx")) return false;
            out_.ins("movq %rax, %xmm0");
            out_.ins("movq %rcx, %xmm1");
            out_.ins("comisd %xmm1, %xmm0");
            const char* fcc = "e";
            switch (inst.op) {
            case IrOpcode::CmpEq: fcc = "e";  break;
            case IrOpcode::CmpNe: fcc = "ne"; break;
            case IrOpcode::CmpLt: fcc = "b";  break;
            case IrOpcode::CmpLe: fcc = "be"; break;
            case IrOpcode::CmpGt: fcc = "a";  break;
            case IrOpcode::CmpGe: fcc = "ae"; break;
            default: break;
            }
            out_.ins(std::string("set") + fcc + " %al");
            out_.ins("movzbq %al, %rax");
            ValueId cr = 0;
            if (!resultValue(instrIdx, cr)) return false;
            return storeReg("rax", cr);
        }
        if (!loadToReg(inst.operands[0], "rax")) return false;
        if (!loadToReg(inst.operands[1], "rcx")) return false;
        out_.ins("cmpq %rcx, %rax");
        const char* cc = "e";
        switch (inst.op) {
        case IrOpcode::CmpEq: cc = "e";  break;
        case IrOpcode::CmpNe: cc = "ne"; break;
        case IrOpcode::CmpLt: cc = "l";  break;
        case IrOpcode::CmpLe: cc = "le"; break;
        case IrOpcode::CmpGt: cc = "g";  break;
        case IrOpcode::CmpGe: cc = "ge"; break;
        default: break;
        }
        out_.ins(std::string("set") + cc + " %al");
        out_.ins("movzbq %al, %rax");
        ValueId result = 0;
        if (!resultValue(instrIdx, result)) return false;
        return storeReg("rax", result);
    }

    bool emitCast(const IrInstr& inst, std::uint32_t instrIdx) {
        if (inst.operands.empty()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "cast: operand kosong");
            return false;
        }
        // ConstantV hasil const-fold: konversi string -> angka via runtime.
        if (inst.operands[0].kind == IrOperandKind::ConstantV &&
            inst.operands[0].id < module_.constants.size()) {
            const IrConstant& cc = module_.constants[inst.operands[0].id];
            if (cc.kind == IrConstantKind::String) {
                const std::string lbl =
                    stringLabel(inst.operands[0].id, instrIdx);
                pushString(lbl, cc);
                const IrType& toC = module_.types[inst.resultType];
                out_.ins("leaq " + lbl + "(%rip), %rdi");
                out_.ins("movq $" + std::to_string(cc.bytes.size()) + ", %rsi");
                if (toC.kind == IrTypeKind::Float) {
                    out_.ins("call rh_atof");   // double di xmm0
                    ValueId fcRes = 0;
                    if (!resultValue(instrIdx, fcRes)) return false;
                    out_.ins("movdqa %xmm0, " +
                             std::to_string(slotOfValue_.at(fcRes)) + "(%rbp)");
                } else {
                    out_.ins("call rh_atoi");   // int di rax
                    ValueId icRes = 0;
                    if (!resultValue(instrIdx, icRes)) return false;
                    if (!storeReg("rax", icRes)) return false;  // home-aware
                }
                return true;
            }
            if (cc.kind == IrConstantKind::FloatBits) {
                std::uint64_t bits = 0;
                for (std::size_t k = 0; k < 8 && k < cc.bytes.size(); ++k) {
                    bits |= static_cast<std::uint64_t>(cc.bytes[k]) << (8 * k);
                }
                out_.ins("movdqa " + floatLabel(bits) + "(%rip), %xmm0");
                out_.ins("cvttsd2si %xmm0, %rax");
                ValueId bcRes = 0;
                if (!resultValue(instrIdx, bcRes)) return false;
                out_.ins("movq %rax, " +
                         std::to_string(slotOfValue_.at(bcRes)) + "(%rbp)");
                return true;
            }
        }
        if (!isRegLoadable(inst.operands[0])) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "cast: operand bukan ValueV/ImmV");
            return false;
        }
        // ImmV (konstanta const-fold): konversi int->int/->float via imm.
        if (inst.operands[0].kind == IrOperandKind::ImmV) {
            const IrType& to0 = module_.types[inst.resultType];
            const std::int64_t imm =
                static_cast<std::int64_t>(inst.operands[0].id);
            ValueId immRes = 0;
            if (!resultValue(instrIdx, immRes)) return false;
            if (to0.kind == IrTypeKind::Float) {
                double dv = static_cast<double>(imm);
                std::uint64_t bits;
                std::memcpy(&bits, &dv, 8);
                out_.ins("movdqa " + floatLabel(bits) + "(%rip), %xmm0");
                out_.ins("movdqa %xmm0, " +
                         std::to_string(slotOfValue_.at(immRes)) + "(%rbp)");
            } else {
                out_.ins("movq $" + std::to_string(imm) + ", %rax");
                if (!storeReg("rax", immRes)) return false;
            }
            return true;
        }
        const IrValue& srcValue = fn_.values[inst.operands[0].id];
        const IrType& from = module_.types[srcValue.type];
        const IrType& to = module_.types[inst.resultType];
        // Float <-> int (f64, D-3): bit slot <-> xmm, cvtsi2sd/cvttsd2si.
        if (from.kind == IrTypeKind::Float || to.kind == IrTypeKind::Float) {
            if (from.kind == to.kind) {
                if (!loadToReg(inst.operands[0], "rax")) return false;
            } else if (from.kind == IrTypeKind::Int) {
                if (!loadToReg(inst.operands[0], "rax")) return false;
                out_.ins("movq %rax, %xmm0");  // movq ke xmm: upper = 0
                out_.ins("cvtsi2sd %rax, %xmm0");
                ValueId castRes = 0;
                if (!resultValue(instrIdx, castRes)) return false;
                out_.ins("movdqa %xmm0, " +
                         std::to_string(slotOfValue_.at(castRes)) + "(%rbp)");
                return true;
            } else {
                if (!loadToReg(inst.operands[0], "rax")) return false;
                out_.ins("movq %rax, %xmm0");
                out_.ins("cvttsd2si %xmm0, %rax");
            }
            ValueId castRes = 0;
            if (!resultValue(instrIdx, castRes)) return false;
            return storeReg("rax", castRes);
        }
        // Bool masuk jalur int (Bool = int 1-byte; narrowing/widening
        // menangani konversi, mis. bilangan -> logika: low byte, 0=salah).
        const bool fromInt = from.kind == IrTypeKind::Int ||
                             from.kind == IrTypeKind::Bool;
        const bool toInt = to.kind == IrTypeKind::Int ||
                           to.kind == IrTypeKind::Bool;
        if (!fromInt || !toInt) {
            fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                 "cast: hanya int<->int dan int<->float");
            return false;
        }
        // Sumber boleh register home (regalloc) atau slot.
        const RegLoc* srcH = home(inst.operands[0].id);
        const auto islot = slotOfValue_.find(inst.operands[0].id);
        if (!srcH && islot == slotOfValue_.end()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "cast: operand tanpa slot/reg");
            return false;
        }
        const std::uint32_t fromB = from.sizeBytes, toB = to.sizeBytes;
        const bool same = fromB == toB;
        const bool sign = typeSignedAt(inst.resultType);
        const std::string src =
            srcH ? ("%" + srcH->reg)
                 : (std::to_string(islot->second) + "(%rbp)");
        if (same) {
            out_.ins("movq " + src + ", %rax");
        } else if (fromB < toB) {
            // Widening: movzx (unsigned) / movsx (signed).
            // Register source WAJIB ber-suffix lebar (movslq %r12d, %rax —
            // bukan %r12: gas reject); memory source tak butuh suffix.
            // Dest movs[bwl]q = r64 — movslq ke %eax INVALID.
            std::string ssrc = srcH ? ("%" + regSized(srcH->reg, fromB)) : src;
            if (fromB == 2) {
                out_.ins(std::string(sign ? "movswq " : "movzwq ") + ssrc + ", %rax");
            } else if (fromB == 4) {
                if (sign) out_.ins("movslq " + ssrc + ", %rax");
                else out_.ins("movl " + ssrc + ", %eax");
            } else {
                out_.ins(std::string(sign ? "movsbq " : "movzbq ") + ssrc + ", %rax");
            }
        } else {
            // narrowing: muat lalu mask lebar tujuan
            out_.ins("movq " + src + ", %rax");
        }
        ValueId result = 0;
        if (!resultValue(instrIdx, result)) return false;
        return storeReg("rax", result);
    }

    // Call & CallIndirect (Fase 3): fungsi user Normal, External user,
    // builtin runtime, dan indirect via FnPtr. Argumen gp System V:
    // skalar <=8B satu reg; View {ptr,len} dua reg; konstanta string via
    // rodata. Argumen stack (gp ke-7+) belum didukung (fail-fast).
    bool emitCall(const IrInstr& inst, std::uint32_t instrIdx) {
        static const char* kGp[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        runtimeBuiltin_ = false;  // di-reset per instruksi call

        const bool indirect = (inst.op == IrOpcode::CallIndirect);
        std::string sym;
        if (indirect) {
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::ValueV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "call_indirect: target ValueV invalid");
                return false;
            }
        } else {
            if (inst.operands.empty() ||
                inst.operands[0].kind != IrOperandKind::FuncV ||
                inst.operands[0].id >= module_.functions.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "call: target FuncV invalid");
                return false;
            }
            const IrFunction& target = module_.functions[inst.operands[0].id];
            if (target.kind == IrFunctionKind::External && target.isBuiltin) {
                const std::optional<std::string> rs = runtimeSymbolFor(target.name);
                if (!rs) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "call: builtin tanpa simbol runtime: " + target.name);
                    return false;
                }
                sym = *rs;
                runtimeBuiltin_ = true;
            } else if (target.kind == IrFunctionKind::Normal ||
                       target.kind == IrFunctionKind::External) {
                sym = asmName(target.name);  // simbol global user
            } else {
                fail(errors_, RhBackendErrorCode::UnsupportedOpcode, fn_.name,
                     "call: kind fungsi tak bisa dipanggil");
                return false;
            }
        }

        // Pass 1 klasifikasi: register gp (maks 6 unit) lalu stack.
        // Unit: skalar = 1, View = 2. Argumen yang tak muat register
        // SELURUHNYA pindah ke stack (8-byte per unit, berurutan) — System V.
        struct ArgPlan { std::uint32_t need; bool stack; std::uint32_t slot; bool xmm; };
        std::vector<ArgPlan> plan;
        // Builtin panjang dengan argumen ARRAY inline: panjang = konstanta
        // arraySize — bukan call rh_len (rh_len khusus View {ptr,len}).
        if (!indirect && sym == "rh_len" && inst.operands.size() == 2 &&
            inst.operands[1].kind == IrOperandKind::ValueV &&
            inst.operands[1].id < fn_.values.size()) {
            const TypeId at = fn_.values[inst.operands[1].id].type;
            if (at != InvalidType && at < module_.types.size() &&
                module_.types[at].kind == IrTypeKind::Array) {
                const auto rv = valueOfInstr_.find(instrIdx);
                if (rv == valueOfInstr_.end()) return true;
                out_.ins("movq $" + std::to_string(module_.types[at].arraySize) +
                         ", %rax");
                return storeReg("rax", rv->second);
            }
        }

        // Builtin tampilkan (rh_print): cetak PER-ARGUMEN BERURUTAN —
        // string -> rh_print(ptr,len); int -> rh_print_int; float ->
        // rh_fprint; teks (View) -> rh_print. Urutan output terjaga
        // karena tiap argumen dicetak sekaligus (runtime tanpa buffer).
        if (!indirect && sym == "rh_print" && inst.operands.size() > 1) {
            for (std::size_t i = 1; i < inst.operands.size(); ++i) {
                const IrOperand& arg = inst.operands[i];
                ConstId cid = InvalidConst;
                if (arg.kind == IrOperandKind::ConstantV) {
                    cid = arg.id;
                } else if (arg.kind == IrOperandKind::ValueV &&
                           arg.id < fn_.values.size()) {
                    const IrValue& v = fn_.values[arg.id];
                    if (v.kind == IrValue::Kind::Instr &&
                        v.payload < fn_.instrs.size()) {
                        const IrInstr& def = fn_.instrs[v.payload];
                        if (def.op == IrOpcode::ConstString &&
                            !def.operands.empty() &&
                            def.operands[0].kind == IrOperandKind::ConstantV) {
                            cid = def.operands[0].id;
                        }
                    }
                }
                // Value const_string yg juga ditulis call @baca (rh_read
                // menulis slot-nya) HARUS baca slot, bukan konstanta asli
                // (bug: baca(nama) -> tampilkan nama = "" padahal slot
                // berisi input baru).
                bool writtenByBaca = false;
                for (const IrInstr& i2 : fn_.instrs) {
                    if (i2.op == IrOpcode::Call && !i2.operands.empty() &&
                        i2.operands[0].kind == IrOperandKind::FuncV &&
                        i2.operands[0].id < module_.functions.size() &&
                        (module_.functions[i2.operands[0].id].name == "baca" ||
                         module_.functions[i2.operands[0].id].name == "baca_file")) {
                        for (std::size_t k = 1; k < i2.operands.size(); ++k) {
                            if (i2.operands[k].kind == IrOperandKind::ValueV &&
                                i2.operands[k].id == arg.id) {
                                writtenByBaca = true;
                            }
                        }
                    }
                }
                const bool isStr =
                    !writtenByBaca &&
                    cid != InvalidConst && cid < module_.constants.size() &&
                    module_.constants[cid].kind == IrConstantKind::String;
                if (isStr) {
                    const IrConstant& c = module_.constants[cid];
                    const std::string lbl = stringLabel(cid, instrIdx);
                    pushString(lbl, c);
                    out_.ins("leaq " + lbl + "(%rip), %rdi");
                    out_.ins("movq $" + std::to_string(c.bytes.size()) + ", %rsi");
                    out_.ins("call rh_print");
                    continue;
                }
                if (arg.kind == IrOperandKind::ConstantV &&
                    cid != InvalidConst &&
                    cid < module_.constants.size() &&
                    module_.constants[cid].kind == IrConstantKind::FloatBits) {
                    // konstanta float hasil const-fold -> rodata 16-byte.
                    const IrConstant& fc = module_.constants[cid];
                    std::uint64_t bits = 0;
                    for (std::size_t k = 0; k < 8 && k < fc.bytes.size(); ++k) {
                        bits |= static_cast<std::uint64_t>(fc.bytes[k]) << (8 * k);
                    }
                    out_.ins("movdqa " + floatLabel(bits) + "(%rip), %xmm0");
                    out_.ins("call rh_fprint");
                    continue;
                }
                if (arg.kind == IrOperandKind::ValueV &&
                    arg.id < fn_.values.size() &&
                    isFloat(module_, fn_.values[arg.id].type)) {
                    const auto fs = slotOfValue_.find(arg.id);
                    if (fs == slotOfValue_.end()) {
                        fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                             fn_.name, "tampilkan: float tanpa slot");
                        return false;
                    }
                    out_.ins("movdqa " + std::to_string(fs->second) +
                             "(%rbp), %xmm0");
                    out_.ins("call rh_fprint");
                    continue;
                }
                if (arg.kind == IrOperandKind::ImmV) {
                    out_.ins("movq $" + std::to_string(arg.id) + ", %rdi");
                    out_.ins("call rh_print_int");
                    continue;
                }
                if (arg.kind == IrOperandKind::ValueV) {
                    if (const RegLoc* h = home(arg.id)) {
                        out_.ins("movq %" + h->reg + ", %rdi");
                        // Tipe reliable dari instr.resultType pembuat nilai.
                        TypeId vt = InvalidType;
                        if (fn_.values[arg.id].kind == IrValue::Kind::Instr &&
                            fn_.values[arg.id].payload < fn_.instrs.size()) {
                            vt = fn_.instrs[fn_.values[arg.id].payload].resultType;
                        }
                        out_.ins(std::string("call ") +
                                 printSymForType(module_, vt));
                        continue;
                    }
                    const auto s = slotOfValue_.find(arg.id);
                    if (s == slotOfValue_.end() || arg.id >= fn_.values.size()) {
                        fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                             fn_.name, "tampilkan: arg tanpa slot");
                        return false;
                    }
                    if (isFat16(module_, fn_.values[arg.id].type)) {
                        const IrType& ty = module_.types[fn_.values[arg.id].type];
                        if (ty.kind == IrTypeKind::View) {
                            // teks: View {ptr,len} (perilaku existing)
                            out_.ins("movq " + std::to_string(s->second) + "(%rbp), %rdi");
                            out_.ins("movq " + std::to_string(s->second - 8) + "(%rbp), %rsi");
                            out_.ins("call rh_print");
                        } else {
                            // Tuple/struct kecil (2 field 8-byte): cetak
                            // "(f0, f1)" per tipe field. Field-0 di <slot>,
                            // field-1 di <slot>-8 (konvensi fat16).
                            // Tipe field: infer dari OPERAND instruksi
                            // Construct (selalu terisi). ty.fields kosong
                            // utk tuple anon -> fallback lama cetak bit
                            // pattern float sbg int (bug tf.rhc).
                            TypeId f0 = TypeId(0), f1 = TypeId(0);
                            const auto& vv0 = fn_.values[arg.id];
                            if (vv0.kind == IrValue::Kind::Instr &&
                                vv0.payload < fn_.instrs.size() &&
                                fn_.instrs[vv0.payload].op == IrOpcode::Construct) {
                                const auto& ops = fn_.instrs[vv0.payload].operands;
                                // Tipe reliable = instr.resultType pembuat
                                // value (values[].type tidak terisi utk
                                // hasil instruksi — pola fix printer).
                                auto tOf = [&](ValueId v) -> TypeId {
                                    const auto& x = fn_.values[v];
                                    if (x.kind == IrValue::Kind::Instr &&
                                        x.payload < fn_.instrs.size()) {
                                        return fn_.instrs[x.payload].resultType;
                                    }
                                    return x.type;
                                };
                                // Layout operand Construct: [0]=metadata
                                // (ConstId), [1]=field-0, [2]=field-1.
                                if (ops.size() > 1) f0 = tOf(ops[1].id);
                                if (ops.size() > 2) f1 = tOf(ops[2].id);
                            } else {
                                if (ty.fields.size() > 0) f0 = ty.fields[0].type;
                                if (ty.fields.size() > 1) f1 = ty.fields[1].type;
                            }
                            emitPrintStr("(");
                            emitFatField(s->second, f0);
                            emitPrintStr(", ");
                            emitFatField(s->second - 8, f1);
                            emitPrintStr(")");
                        }
                    } else {
                        out_.ins("movq " + std::to_string(s->second) + "(%rbp), %rdi");
                        // Tipe nilai: fn_.values[v].type tidak terisi utk
                        // hasil instruksi; tipe reliable ada di
                        // instr.resultType (instrIdx = values[v].payload).
                        TypeId vt = InvalidType;
                        if (arg.id < fn_.values.size() &&
                            fn_.values[arg.id].kind == IrValue::Kind::Instr &&
                            fn_.values[arg.id].payload < fn_.instrs.size()) {
                            vt = fn_.instrs[fn_.values[arg.id].payload].resultType;
                        }
                        out_.ins(std::string("call ") +
                                 printSymForType(module_, vt));
                    }
                    continue;
                }
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "tampilkan: bentuk argumen tak didukung");
                return false;
            }
            return true;  // tampilkan void
        }
        // ---- baca: variadic — string arg -> prompt (rh_print);
        // argumen lain -> rh_read(slot, typeId) ----
        if (sym == "rh_read") {
            for (std::size_t i = 1; i < inst.operands.size(); ++i) {
                const IrOperand& arg = inst.operands[i];
                // (a) string konstan -> prompt (pola ConstString tampilkan)
                // HANYA argumen PERTAMA (i==1) DAN ada var lain (size>2): tanpa
                // ini, variabel teks yg fold ke const_string ikut tertangkap
                // sbg prompt -> rh_read tak pernah dipanggil (baca(nama)=[]).
                const bool canBePrompt = (i == 1 && inst.operands.size() > 2);
                ConstId cid = InvalidConst;
                if (arg.kind == IrOperandKind::ConstantV) {
                    cid = arg.id;
                } else if (arg.kind == IrOperandKind::ValueV &&
                           arg.id < fn_.values.size()) {
                    const IrValue& v = fn_.values[arg.id];
                    if (v.kind == IrValue::Kind::Instr &&
                        v.payload < fn_.instrs.size()) {
                        const IrInstr& def = fn_.instrs[v.payload];
                        if (def.op == IrOpcode::ConstString &&
                            !def.operands.empty() &&
                            def.operands[0].kind == IrOperandKind::ConstantV) {
                            cid = def.operands[0].id;
                        }
                    }
                }
                const bool isStr =
                    canBePrompt &&
                    cid != InvalidConst && cid < module_.constants.size() &&
                    module_.constants[cid].kind == IrConstantKind::String;
                if (isStr) {
                    const IrConstant& c = module_.constants[cid];
                    const std::string lbl = stringLabel(cid, instrIdx);
                    pushString(lbl, c);
                    out_.ins("leaq " + lbl + "(%rip), %rdi");
                    out_.ins("movq $" + std::to_string(c.bytes.size()) + ", %rsi");
                    out_.ins("call rh_print");
                    continue;
                }
                // (b) variabel target -> rh_read(slot, typeId)
                if (arg.kind != IrOperandKind::ValueV) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "baca: bentuk argumen tak didukung");
                    return false;
                }
                std::string sl;
                const auto s = slotOfValue_.find(arg.id);
                if (s != slotOfValue_.end()) {
                    sl = std::to_string(s->second);
                } else if (const RegLoc* h = home(arg.id)) {
                    // Spill home ke scratch, reload setelah rh_read.
                    out_.ins("movq %" + h->reg + ", " +
                             std::to_string(bacaScratch_) + "(%rbp)");
                    sl = std::to_string(bacaScratch_);
                    reloadReg_ = h->reg;
                } else {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "baca: target tanpa stack slot/home");
                    return false;
                }
                // typeId: 0=int 1=uint 2=karakter 3=logika 4=desimal 5=teks
                int tId = 0;
                TypeId vt = InvalidType;
                if (fn_.values[arg.id].kind == IrValue::Kind::Instr &&
                    fn_.values[arg.id].payload < fn_.instrs.size()) {
                    vt = fn_.instrs[fn_.values[arg.id].payload].resultType;
                }
                if (vt != InvalidType && vt < module_.types.size()) {
                    const IrType& ty = module_.types[vt];
                    if (ty.kind == IrTypeKind::Bool) tId = 3;
                    else if (ty.kind == IrTypeKind::Float) tId = 4;
                    else if (ty.kind == IrTypeKind::View) tId = 5;
                    else if (ty.kind == IrTypeKind::Int) {
                        if (!ty.isSigned) tId = 1;
                        if (ty.name == "karakter") tId = 2;
                    }
                }
                out_.ins("leaq " + sl + "(%rbp), %rdi");
                out_.ins("movq $" + std::to_string(tId) + ", %rsi");
                out_.ins("call rh_read");
                if (!reloadReg_.empty()) {
                    out_.ins("movq " + sl + "(%rbp), %" + reloadReg_);
                    reloadReg_.clear();
                }
            }
            return true;  // baca void
        }
        // ---- baca_file(path, isi): path View by-value, isi OUTPUT
        // PARAM (alamat slot View ditulis rh_read_file) — TIDAK bisa
        // lewat call handler umum (View by value 2 reg -> rh_read_file
        // menulis ke alamat View = corrupt). Dispatcher custom: mirip
        // pola baca. Return int sukses di rax -> slot hasil.
        if (sym == "rh_read_file") {
            if (inst.operands.size() != 3) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "baca_file: butuh (path, isi)");
                return false;
            }
            // arg1: path (konstanta string atau View value)
            const IrOperand& pa = inst.operands[1];
            ConstId pcid = InvalidConst;
            if (pa.kind == IrOperandKind::ConstantV) {
                pcid = pa.id;
            } else if (pa.kind == IrOperandKind::ValueV &&
                       pa.id < fn_.values.size()) {
                const IrValue& pv = fn_.values[pa.id];
                if (pv.kind == IrValue::Kind::Instr &&
                    pv.payload < fn_.instrs.size()) {
                    const IrInstr& def = fn_.instrs[pv.payload];
                    if (def.op == IrOpcode::ConstString &&
                        !def.operands.empty() &&
                        def.operands[0].kind == IrOperandKind::ConstantV) {
                        pcid = def.operands[0].id;
                    }
                }
            }
            if (pcid != InvalidConst && pcid < module_.constants.size() &&
                module_.constants[pcid].kind == IrConstantKind::String) {
                const IrConstant& c = module_.constants[pcid];
                const std::string lbl = stringLabel(pcid, instrIdx);
                pushString(lbl, c);
                out_.ins("leaq " + lbl + "(%rip), %rdi");
                out_.ins("movq $" + std::to_string(c.bytes.size()) + ", %rsi");
            } else if (pa.kind == IrOperandKind::ValueV) {
                const auto ps = slotOfValue_.find(pa.id);
                if (ps == slotOfValue_.end()) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "baca_file: path tanpa slot");
                    return false;
                }
                out_.ins("movq " + std::to_string(ps->second) + "(%rbp), %rdi");
                out_.ins("movq " + std::to_string(ps->second - 8) + "(%rbp), %rsi");
            } else {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "baca_file: path tak didukung");
                return false;
            }
            // arg2: isi — ALAMAT slot (output param)
            const IrOperand& ia = inst.operands[2];
            if (ia.kind != IrOperandKind::ValueV) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "baca_file: isi bukan variabel");
                return false;
            }
            const auto islot = slotOfValue_.find(ia.id);
            if (islot == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "baca_file: isi tanpa slot");
                return false;
            }
            out_.ins("leaq " + std::to_string(islot->second) + "(%rbp), %rdx");
            out_.ins("call rh_read_file");
            // return int sukses (rax) -> slot hasil call (pola return
            // handler umum di emitCall: via valueOfInstr_).
            const auto rv = valueOfInstr_.find(instrIdx);
            if (rv != valueOfInstr_.end()) {
                const auto s2 = slotOfValue_.find(rv->second);
                if (s2 != slotOfValue_.end()) {
                    out_.ins("movq %rax, " +
                             std::to_string(s2->second) + "(%rbp)");
                } else if (const RegLoc* h = home(rv->second)) {
                    // hasil call di-home-kan: rax -> register home
                    // (nilai baru), bukan slot (home values tak punya).
                    out_.ins("movq %rax, %" + h->reg);
                }
            }
            return true;
        }
        // ---- gabung(a, b): copy dua larik ke slot hasil call
        // (output-via-slot, pola baca_file). Argumen agregat >16B tak
        // bisa lewat call handler umum (MEMORY class konvensi rumit).
        if (sym == "rh_concat_array_to") {
            auto tOf = [&](ValueId v) -> TypeId {
                const auto& x = fn_.values[v];
                if (x.kind == IrValue::Kind::Instr &&
                    x.payload < fn_.instrs.size()) {
                    return fn_.instrs[x.payload].resultType;
                }
                return x.type;
            };
            const char* preg[2] = {"rdi", "rdx"};
            const char* lreg[2] = {"rsi", "rcx"};
            for (std::size_t k = 0; k < 2; ++k) {
                const IrOperand& a = inst.operands[1 + k];
                if (a.kind != IrOperandKind::ValueV) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "gabung: argumen bukan ValueV");
                    return false;
                }
                const auto s = slotOfValue_.find(a.id);
                if (s == slotOfValue_.end()) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "gabung: arg tanpa slot");
                    return false;
                }
                out_.ins(std::string("leaq ") +
                         std::to_string(s->second) + "(%rbp), %" +
                         preg[k]);
                out_.ins(std::string("movq $") +
                         std::to_string(typeSizeAt(tOf(a.id))) +
                         ", %" + lreg[k]);
            }
            // slot hasil call via valueOfInstr_ (pola rh_read_file)
            const auto rv = valueOfInstr_.find(instrIdx);
            if (rv == valueOfInstr_.end()) return true;  // void
            const auto rs = slotOfValue_.find(rv->second);
            if (rs == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "gabung: hasil tanpa slot");
                return false;
            }
            out_.ins("leaq " + std::to_string(rs->second) + "(%rbp), %r8");
            out_.ins("call rh_concat_array_to");
            return true;
        }

        std::uint32_t reg = 0;
        std::uint32_t stk = 0;
        std::uint32_t xmmN = 0;
        for (std::size_t i = 1; i < inst.operands.size(); ++i) {
            const IrOperand& arg = inst.operands[i];
            std::uint32_t need = 1;
            bool stackForce = false;
            if (arg.kind == IrOperandKind::ConstantV) {
                if (arg.id < module_.constants.size() &&
                    module_.constants[arg.id].kind == IrConstantKind::String) {
                    need = 2;
                }
            } else if (arg.kind == IrOperandKind::ValueV &&
                       arg.id < fn_.values.size()) {
                const IrValue& v = fn_.values[arg.id];
                // Tipe reliable dari instr.resultType pembuat value
                // (v.type tidak terisi utk hasil instruksi — bug: arg
                // fat16 call handler terbaca size 0, len tak dipass,
                // tulis_file gagal: io.rhc).
                TypeId at = v.type;
                if (v.kind == IrValue::Kind::Instr &&
                    v.payload < fn_.instrs.size()) {
                    at = fn_.instrs[v.payload].resultType;
                }
                if (isFloat(module_, at)) {
                    // Kelas SSE: argumen f64 -> xmm0-7. Runtime builtin
                    // belum menerima float (rh_print tak bisa format).
                    if (runtimeBuiltin_) {
                        fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                             "runtime belum mendukung argumen float");
                        return false;
                    }
                    if (xmmN >= 8) {
                        fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                             "call: argumen float melebihi 8 register xmm");
                        return false;
                    }
                    plan.push_back({1, false, 0, true});
                    ++xmmN;
                    continue;
                }
                const std::uint64_t sz = typeSizeAt(at);
                if (sz > 8) {
                    if (sz <= 16 && isFat16(module_, at)) {
                        need = 2;  // agregat <=16B: dua register (System V)
                    } else if (at != InvalidType &&
                               at < module_.types.size() &&
                               (module_.types[at].kind == IrTypeKind::Array ||
                                module_.types[at].kind == IrTypeKind::Struct ||
                                module_.types[at].kind == IrTypeKind::View ||
                                module_.types[at].kind == IrTypeKind::Sum)) {
                        // Agregat >16B: MEMORY class — seluruhnya ke stack.
                        need = static_cast<std::uint32_t>((sz + 7) / 8);
                        stackForce = true;
                    } else {
                        fail(errors_, RhBackendErrorCode::UnsupportedType, fn_.name,
                             "call: argumen tipe >8 byte bukan agregat");
                        return false;
                    }
                }
            }
            if (!stackForce && reg + need <= 6) {
                plan.push_back({need, false, 0, false});
                reg += need;
            } else {
                plan.push_back({need, true, stk, false});
                stk += need;
            }
        }
        const std::uint32_t stkBytes = (stk * 8 + 15) / 16 * 16;
        if (stkBytes > 0) {
            out_.ins("subq $" + std::to_string(stkBytes) + ", %rsp");
        }

        // Pass 2 emisi: resolve nilai ke scratch r10 (ptr) / r11 (len),
        // lalu tempatkan ke stack slot atau register gp.
        reg = 0;
        std::uint32_t xmmEmi = 0;
        for (std::size_t i = 1; i < inst.operands.size(); ++i) {
            const IrOperand& arg = inst.operands[i];
            const ArgPlan& ap = plan[i - 1];

            // Resolve: r10 = nilai/ptr, r11 = len (View & string const).
            ConstId cid = InvalidConst;
            if (arg.kind == IrOperandKind::ConstantV) {
                cid = arg.id;
            } else if (arg.kind == IrOperandKind::ValueV &&
                       arg.id < fn_.values.size()) {
                const IrValue& v = fn_.values[arg.id];
                if (v.kind == IrValue::Kind::Instr && v.payload < fn_.instrs.size()) {
                    const IrInstr& def = fn_.instrs[v.payload];
                    if (def.op == IrOpcode::ConstString && !def.operands.empty() &&
                        def.operands[0].kind == IrOperandKind::ConstantV) {
                        cid = def.operands[0].id;
                    }
                }
            }
            const bool isStringConst =
                cid != InvalidConst && cid < module_.constants.size() &&
                module_.constants[cid].kind == IrConstantKind::String;
            bool resolved = false;
            if (isStringConst) {
                const IrConstant& c = module_.constants[cid];
                const std::string lbl = stringLabel(cid, instrIdx);
                pushString(lbl, c);
                out_.ins("leaq " + lbl + "(%rip), %r10");
                out_.ins("movq $" + std::to_string(c.bytes.size()) + ", %r11");
                resolved = true;
            } else if (arg.kind == IrOperandKind::ImmV) {
                out_.ins("movq $" + std::to_string(arg.id) + ", %r10");
                resolved = true;
            } else if (arg.kind == IrOperandKind::ValueV) {
                const IrValue& v = fn_.values[arg.id];
                if (const RegLoc* h = home(arg.id)) {
                    out_.ins("movq %" + h->reg + ", %r10");
                } else {
                    const auto s = slotOfValue_.find(arg.id);
                    if (s == slotOfValue_.end() || arg.id >= fn_.values.size()) {
                        fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                             "call: arg ValueV tanpa slot");
                        return false;
                    }
                    out_.ins("movq " + std::to_string(s->second) + "(%rbp), %r10");
                    if (typeSizeAt(v.type) > 8) {
                        out_.ins("movq " + std::to_string(s->second - 8) +
                                 "(%rbp), %r11");
                    }
                }
                resolved = true;
            }
            if (!resolved) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "call: bentuk argumen tak didukung");
                return false;
            }

            // Tempatkan.
            if (ap.xmm) {
                const auto fs = slotOfValue_.find(arg.id);
                if (fs == slotOfValue_.end()) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "call: arg float tanpa slot");
                    return false;
                }
                out_.ins("movdqa " + std::to_string(fs->second) +
                         "(%rbp), %xmm" + std::to_string(xmmEmi++));
                continue;
            }
            if (ap.stack) {
                if (ap.need <= 2) {
                    out_.ins("movq %r10, " + std::to_string(ap.slot * 8) + "(%rsp)");
                    if (ap.need == 2) {
                        out_.ins("movq %r11, " + std::to_string(ap.slot * 8 + 8) +
                                 "(%rsp)");
                    }
                } else {
                    // Agregat MEMORY class: salin semua unit dari slot.
                    const auto s = slotOfValue_.find(arg.id);
                    if (s == slotOfValue_.end()) {
                        fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                             "call: agregat stack tanpa slot");
                        return false;
                    }
                    for (std::int32_t u = 0; u < static_cast<std::int32_t>(ap.need); ++u) {
                        out_.ins("movq " + std::to_string(s->second - 8 * u) +
                                 "(%rbp), %rax");
                        out_.ins("movq %rax, " +
                                 std::to_string(ap.slot * 8 + 8 * u) + "(%rsp)");
                    }
                }
            } else {
                out_.ins(std::string("movq %r10, %") + kGp[reg]);
                if (ap.need == 2) {
                    out_.ins(std::string("movq %r11, %") + kGp[reg + 1]);
                }
                reg += ap.need;
            }
        }

        // System V variadic: al = jumlah register xmm terpakai.
        out_.ins("movq $" + std::to_string(xmmEmi) + ", %rax");
        if (indirect) {
            const auto s = slotOfValue_.find(inst.operands[0].id);
            if (s == slotOfValue_.end()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "call_indirect: target tanpa slot");
                return false;
            }
            out_.ins("movq " + std::to_string(s->second) + "(%rbp), %r11");
            out_.ins("call *%r11");
        } else {
            out_.ins("call " + sym);
        }
        if (stkBytes > 0) {
            out_.ins("addq $" + std::to_string(stkBytes) + ", %rsp");
        }
        // Hasil call (bila ada) -> slot.
        const auto rv = valueOfInstr_.find(instrIdx);
        if (rv != valueOfInstr_.end()) {
            // Tipe reliable dari instr.resultType (values[].type tidak
            // terisi utk hasil instruksi).
            const TypeId rt = inst.resultType;
            if (typeSizeAt(rt) > 8) {
                if (!isFat16(module_, rt)) {
                    fail(errors_, RhBackendErrorCode::UnsupportedType,
                         fn_.name, "hasil call >16 byte belum didukung");
                    return false;
                }
                // Return fat16 (SysV ABI struct <=16B): kata-0 di %rax,
                // kata-1 di %rdx -> slot & slot-8 (konvensi fat16).
                const auto s2 = slotOfValue_.find(rv->second);
                if (s2 == slotOfValue_.end()) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand,
                         fn_.name, "hasil call fat16 tanpa slot");
                    return false;
                }
                out_.ins("movq %rax, " + std::to_string(s2->second) + "(%rbp)");
                out_.ins("movq %rdx, " +
                         std::to_string(s2->second - 8) + "(%rbp)");
                return true;
            }
            if (isFloat(module_, rt)) {
                // return f64 di xmm0 -> slot 16-byte (movdqa).
                out_.ins("movdqa %xmm0, " +
                         std::to_string(slotOfValue_.at(rv->second)) + "(%rbp)");
                return true;
            }
            return storeReg("rax", rv->second);
        }
        return true;
    }

    bool emitSwitchInt(const IrInstr& inst, std::uint32_t instrIdx,
                       std::uint32_t blockOrdinal) {
        if (inst.operands.empty() || !loadToReg(inst.operands[0], "rax")) {
            return false;
        }
        // Pasangan (nilai, BlockV) lalu BlockV default di operand terakhir.
        const std::size_t n = inst.operands.size();
        if (n < 2) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "switch_int: operand kurang");
            return false;
        }
        // Nilai kasus: ImmV / ConstantV(const_int) / ValueV(const_int SSA).
        for (std::size_t k = 1; k + 1 < n; k += 2) {
            const IrOperand& valOp = inst.operands[k];
            const IrOperand& blkOp = inst.operands[k + 1];
            std::uint64_t caseValue = 0;
            if (valOp.kind == IrOperandKind::ImmV) {
                caseValue = valOp.id;
            } else if (valOp.kind == IrOperandKind::ValueV &&
                       valOp.id < fn_.values.size()) {
                const IrValue& v = fn_.values[valOp.id];
                if (v.kind != IrValue::Kind::Instr || v.payload >= fn_.instrs.size() ||
                    !decodeConstInt(fn_.instrs[v.payload], caseValue)) {
                    fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                         "switch_int: nilai kasus bukan konstanta");
                    return false;
                }
            } else {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "switch_int: nilai kasus tak dikenal");
                return false;
            }
            if (blkOp.id >= fn_.blocks.size()) {
                fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                     "switch_int: block tak dikenal");
                return false;
            }
            out_.ins("movq $" + std::to_string(caseValue) + ", %rcx");
            out_.ins("cmpq %rcx, %rax");
            const std::string next = ".LSw" + std::to_string(instrIdx) + "_" +
                                     std::to_string(k) + "_" + fn_.name;
            out_.ins("jne " + next);
            if (!emitPhiMoves(blockOrdinal, blkOp.id)) return false;
            out_.ins("jmp " + blockLabel(blkOp.id));
            out_.label(next);
        }
        if (inst.operands[n - 1].id >= fn_.blocks.size()) {
            fail(errors_, RhBackendErrorCode::UnsupportedOperand, fn_.name,
                 "switch_int: default tak dikenal");
            return false;
        }
        if (!emitPhiMoves(blockOrdinal, inst.operands[n - 1].id)) return false;
        out_.ins("jmp " + blockLabel(inst.operands[n - 1].id));
        return true;
    }

    const IrModule& module_;
    const IrFunction& fn_;
    AsmBuilder& out_;
    std::vector<RhBackendError>& errors_;
    std::vector<PendingString>* pendingStrings_ = nullptr;
    const RegAllocResult* alloc_ = nullptr;
    std::uint32_t shift_ = 0;   // 8 * jumlah callee-saved terpakai
    std::vector<PMove> moves_;  // koleksi move phi per-edge (di-clear per edge)
    std::vector<std::pair<std::string, std::uint64_t>>* pendingFloats_ = nullptr;
    std::unordered_map<std::uint64_t, std::string> floatLabels_;

    std::unordered_map<std::uint32_t, ValueId> valueOfInstr_;
    std::unordered_map<ValueId, std::int32_t> slotOfValue_;
    std::unordered_map<ConstId, std::string> stringLabels_;
    std::uint32_t tupleStrCounter_ = 0;
    std::int32_t bacaScratch_ = 0;
    std::string reloadReg_;
    std::uint32_t slotCount_ = 0;
    std::int32_t frameSize_ = 0;
    std::uint32_t allocArea_ = 0;
    std::uint32_t allocUsed_ = 0;
    bool typeFailed_ = false;
    bool runtimeBuiltin_ = false;  // call target = builtin runtime (satu call)
};

}  // namespace

bool X86_64SysVEmitter::emitModule(const FinalIr& finalIr, AsmBuilder& out,
                                   std::vector<RhBackendError>& errors,
                                   bool emitCrtGlue) {
    const IrModule& module = finalIr.module();
    // RegAlloc: graph coloring (Chaitin) — satu-satunya mode.
    const bool useGraphAlloc = true;
    std::unordered_map<FuncId, RegAllocResult> allocs;
    out.comment("Rheinc RhBackend - x86_64-linux-sysv (naive stack-slot)");
    out.text();

    // Variabel global mutable -> .bss (nol-inisialisasi; nilai non-literal
    // dibangun oleh __global_init lewat Store @G).
    if (!module.globalVars.empty()) {
        out.section(".bss");
        for (std::uint32_t g = 0; g < module.globalVars.size(); ++g) {
            const IrGlobal& gv = module.globalVars[g];
            const std::uint64_t sz = gv.type != InvalidType &&
                                             gv.type < module.types.size()
                                         ? module.types[gv.type].sizeBytes
                                         : 8;
            out.align(8);
            out.globl_label(globalLabelName(module, g));
            out.label(globalLabelName(module, g));
            const std::uint32_t gsz = static_cast<std::uint32_t>(sz == 0 ? 8 : sz);
            out.zero_bytes(gsz);
            out.size_sym(globalLabelName(module, g), std::to_string(gsz));
        }
        out.text();
    }

    std::vector<PendingString> pendingStrings;
    std::vector<std::pair<std::string, std::uint64_t>> pendingFloats;
    bool emittedGlobalInit = false;
    for (const IrFunction& fn : module.functions) {
        if (fn.kind == IrFunctionKind::External) {
            continue;  // stub: simbol didefinisikan di luar (runtime)
        }
        if (fn.kind == IrFunctionKind::GlobalInit && !emitCrtGlue) {
            continue;  // object dependency: init global dipegang object utama
        }
        if (fn.kind == IrFunctionKind::GlobalInit) emittedGlobalInit = true;
        FnEmitter fe(module, fn, out, errors);
        const RegAllocResult* allocPtr = nullptr;
        if (useGraphAlloc && fn.kind != IrFunctionKind::External) {
            RegAllocResult ar = createGraphColoringRegAlloc()->allocate(
                module, fn, errors);
            if (!ar.ok) return false;
            allocs.emplace(static_cast<FuncId>(
                               &fn - module.functions.data()),
                           std::move(ar));
            allocPtr = &allocs.find(static_cast<FuncId>(
                                        &fn - module.functions.data()))
                            ->second;
        }
        fe.setRegAlloc(allocPtr);
        if (!fe.run(pendingStrings, pendingFloats)) {
            return false;
        }
    }
    // Stub start: crt memanggil stub (bukan utama langsung) sehingga
    // exit code DETERMINISTIK — stub tahu tipe return entry: void -> 0,
    // nilai -> rax. (Tanpa stub, exit code = sisa rax terakhir = garbage
    // yang berbeda antar mode optimisasi.)
    if (emitCrtGlue) {
        const FuncId entry = finalIr.entryFunction();
        // Void atau tidak: infer dari terminator entry (Ret vs RetVoid) —
        // IrFunction tak menyimpan returnType langsung.
        bool voidRet = true;
        if (entry != InvalidFunc) {
            for (const IrInstr& ei : module.functions[entry].instrs) {
                if (ei.op == IrOpcode::Ret) { voidRet = false; break; }
                if (ei.op == IrOpcode::RetVoid) { voidRet = true; break; }
            }
        }
        std::string entryName = "utama";
        if (entry != InvalidFunc) {
            entryName = module.functions[entry].name;
            for (char& c : entryName) {
                const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '_';
                if (!ok) c = '_';
            }
        }
        out.globl("__rheinc_stub");
        out.typeFunction("__rheinc_stub");
        out.label("__rheinc_stub");
        // ABI System V: rsp harus 16-byte aligned SEBELUM call. _start
        // manggil stub saat rsp≡8 (mod 16) -> andq merapatkan sehingga
        // fungsi target melihat rsp≡8 di entry (standar). Tanpa ini,
        // movdqa ke slot rbp-X fault (rbp tak 16-aligned).
        out.ins("andq $-16, %rsp");
        // initialisasi global HARUS jalan sebelum entry (DEVIATIONS:
        // call __global_init dulu sebelum call utama — stub selalu call
        // __global_init bila emittedGlobalInit; bila modul tak punya init,
        // fallback kosong retq di bawah tetap aman dipanggil).
        if (emittedGlobalInit) {
            out.ins("call __global_init");
        }
        if (entry != InvalidFunc) {
            out.ins("call " + entryName);
        }
        if (voidRet) {
            out.ins("xorl %edi, %edi");
        } else {
            out.ins("movq %rax, %rdi");
        }
        out.ins("movq $60, %rax");   // SYS_exit
        out.ins("syscall");
    }

    // __global_init selalu ada (crt memanggilnya); kosong bila modul tak
    // punya inisialisasi global.
    if (emitCrtGlue && !emittedGlobalInit) {
        out.globl("__global_init");
        out.typeFunction("__global_init");
        out.label("__global_init");
        out.ins("retq");
    }

    out.rodata();
    for (const auto& pf : pendingFloats) {
        out.align(16);
        out.label(pf.first);
        out.quad(pf.second);
        out.quad(pf.second);
    }
    for (const PendingString& ps : pendingStrings) {
        out.align(1);
        out.label(ps.label);
        out.asciiZ(
            std::string(ps.constant.bytes.begin(), ps.constant.bytes.end()));
    }

    out.gnuStackNote();
    return true;
}

}  // namespace rhc
