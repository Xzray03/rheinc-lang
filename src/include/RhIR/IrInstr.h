#pragma once

#include "RhIR/IrIds.h"
#include "RhCore/SourceLocation.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace rhc {

// Daftar opcode IR lengkap. Kategori mengikuti spesifikasi Bagian 2.2;
// daftar operator lengkap mengikuti analisis TokenKind Rheinc
// (aritmetika, bitwise, logika, perbandingan, dsb.).
//
// Operand layout per opcode (indeks operand ke-i, kind ditulis dalam kurung):
//
//   ConstInt      [0](ImmV)                     -> imm integer inline
//   ConstFloat    [0](ImmV)                     -> bit pattern inline
//   ConstBool     [0](ImmV)                     -> 0 / 1
//   ConstString   [0](ConstantV)                -> ConstId string di pool
//   ConstNull     []                            -> pointer null
//   Alloc         [0](TypeV)                    -> alokasi instance tipe
//   Load          [0](ValueV ptr | GlobalV g)   -> baca dari alamat/global
//   Store         [0](ValueV ptr | GlobalV g) [1](ValueV v) -> tulis v ke alamat/global
//   CopyConst     [0](ValueV dst) [1](ConstantV)-> salin data pool ke memory
//   AddrOf        [0](ValueV target | FuncV f)  -> alamat objek / fn ptr (alamat x, fungsi sebagai nilai)
//   Deref         [0](ValueV ptr)               -> isi(ptr): baca nilai pointee
//   FieldAddr     [0](ValueV base) [1](TypeV) [2](ImmV idx)
//                                                -> alamat field ke-idx struct
//   Construct     [0](TypeV) lalu ValueV/ConstantV per elemen
//   Extract       [0](ValueV agg) [1](ImmV|ValueV idx) -> baca field/elemen
//   Insert        [0](ValueV agg) [1](ImmV|ValueV idx) [2](ValueV v)
//                                                -> value baru; agg lama utuh
//   (binop arit/bit/logika/cmp) [0](ValueV a) [1](ValueV b)
//   (unop)          [0](ValueV a)
//   Cast            [0](ValueV v) [1](TypeV target)
//   Call          [0](FuncV) lalu ValueV/ConstantV per argumen
//   CallIndirect  [0](ValueV fnptr) lalu argumen
//   Phi           pasangan berurut: [2k](BlockV pred) [2k+1](ValueV v)
//   MatchTag      [0](ValueV sum) lalu pasangan (ValueV tag const-instr,
//                 BlockV target), terakhir [n](BlockV default)
//   SwitchInt     [0](ValueV subjek) lalu pasangan (ConstantV nilai, BlockV),
//                 terakhir [n](BlockV default)
//   Br            [0](BlockV target)
//   CondBr        [0](ValueV kond) [1](BlockV then) [2](BlockV else)
//   Ret           [0](ValueV nilai)
//   RetVoid       []
//   Unreachable   []
//   Dead          (tombstone; operand diabaikan)
enum class IrOpcode : std::uint16_t {
    // konstanta sebagai instruksi SSA (hasil = SSA value)
    ConstInt,
    ConstFloat,
    ConstBool,
    ConstString,
    ConstNull,

    // memory
    Alloc,
    Load,
    Store,
    CopyConst,

    // operasi alamat
    AddrOf,
    Deref,
    FieldAddr,

    // aggregate first-class (spesifikasi Bagian 3.2)
    Construct,
    Extract,
    Insert,

    // aritmetika & bitwise
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Pow,        // ** (StarStar)
    DivFloor,   // // (SlashSlash)
    BitAnd,
    BitOr,
    BitXor,
    Shl,        // << (LessLess)
    Shr,        // >> (GreaterGreater)
    Neg,
    BitNot,     // ~ (Tilde)
    Inc,
    Dec,

    // logika
    And,        // dan (KwDan)
    Or,         // atau (KwAtau)
    Not,        // bukan (UnaryOp::LogicalNot)

    // perbandingan
    CmpEq,
    CmpNe,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,

    // append buffer teks: a +> b -> buffer baru a+b (memory-style)
    Append,

    // perkalian matriks (a @ b)
    MatMul,

    // panjang larik/view (hasil Int)
    Len,
    // view baru dari larik/teks: [0]src [1]start [2]end [3]step (hasil View)
    SliceView,

    // konversi tipe (casting.rhc / konversi_tipe.rhc)
    Cast,

    // pemanggilan fungsi
    Call,
    CallIndirect,

    // SSA
    Phi,

    // sum type (spesifikasi Bagian 3.7)
    MatchTag,

    // terminator — setiap block tepat satu, di posisi terakhir
    // (invariant; spesifikasi Bagian 4.3)
    Br,
    CondBr,
    SwitchTag,
    SwitchInt,
    Ret,
    RetVoid,
    Unreachable,

    // lifecycle: tombstone penghapusan, BUKAN opcode semantik
    // (spesifikasi Bagian 2.4)
    Dead,
};

enum class IrOperandKind : std::uint8_t {
    ValueV,     // id adalah ValueId
    ConstantV,  // id adalah ConstId
    BlockV,     // id adalah BlockId
    FuncV,      // id adalah FuncId
    TypeV,      // id adalah TypeId (operand bertipe, e.g. alloc)
    ImmV,       // id adalah immediate inline (const_int/const_float/const_bool);
                // integer biasa TIDAK masuk constant pool (spesifikasi 6.2)
    GlobalV,    // id adalah indeks IrModule::globalVars (storage global);
                // dipakai sebagai ptr oleh Load/Store
};

struct IrOperand {
    IrOperandKind kind;
    std::uint32_t id;

    constexpr IrOperand() : kind(IrOperandKind::ValueV), id(0) {}
    constexpr IrOperand(IrOperandKind operandKind, std::uint32_t operandId)
        : kind(operandKind), id(operandId) {}

    // Helper konstruktor per kind — membuat call-site eksplisit & aman.
    static constexpr IrOperand value(ValueId valueId)     { return {IrOperandKind::ValueV, valueId}; }
    static constexpr IrOperand constant(ConstId constId)  { return {IrOperandKind::ConstantV, constId}; }
    static constexpr IrOperand block(BlockId blockId)     { return {IrOperandKind::BlockV, blockId}; }
    static constexpr IrOperand func(FuncId funcId)        { return {IrOperandKind::FuncV, funcId}; }
    static constexpr IrOperand type(TypeId typeId)        { return {IrOperandKind::TypeV, typeId}; }
    static constexpr IrOperand global(std::uint32_t idx)  { return {IrOperandKind::GlobalV, idx}; }
    static constexpr IrOperand imm(std::uint32_t immValue){ return {IrOperandKind::ImmV, immValue}; }
};

struct IrInstr {
    IrOpcode op;
    TypeId resultType = InvalidType;    // InvalidType = instruksi tanpa result
    SourceLocation loc = 0;             // dari SourceManager (uint64_t)
    std::vector<IrOperand> operands;    // variable-length per instruksi
    std::string name;                   // nama symbol asli; boleh kosong
    std::uint32_t epoch = 0;            // epoch pembuatan/pembaruan terakhir
};

// true jika opcode adalah terminator (harus menjadi instruksi terakhir block).
inline constexpr bool isTerminator(IrOpcode op) {
    switch (op) {
    case IrOpcode::Br:
    case IrOpcode::CondBr:
    case IrOpcode::SwitchTag:
    case IrOpcode::SwitchInt:
    case IrOpcode::MatchTag:
    case IrOpcode::Ret:
    case IrOpcode::RetVoid:
    case IrOpcode::Unreachable:
        return true;
    default:
        return false;
    }
}

} // namespace rhc
