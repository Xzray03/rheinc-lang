#pragma once

#include "RhAST/IrIds.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rhc {

// Jenis tipe IR. Tipe kompleks dari corpus (bilangan_kompleks,
// cacahan/fixed point, matriks) tidak perlu kind khusus; cukup
// direpresentasikan sebagai Struct/Array hasil lowering
// (spesifikasi Bagian 5.2).
enum class IrTypeKind : std::uint8_t {
    Void,
    Bool,
    Int,        // signed/unsigned; lebar = sizeBytes (1/2/4/8)
    Float,      // f32 / f64 (sizeBytes 4/8)
    Pointer,
    Array,      // fixed-size, elemen homogen
    View,       // ptr + len (hasil slicing, buffer teks runtime)
    Struct,     // named, fields, dapat recursive
    Sum,        // tag + payload per varian
    FnPtr,      // signature fungsi
};

// Field struct/sum (spesifikasi Bagian 5.3).
struct IrField {
    std::string name;
    TypeId type = InvalidType;
    std::uint32_t offsetBytes = 0;    // byte offset base
    std::uint8_t bitOffset = 0;       // offset bit di dalam byte (bitfield)
    std::uint8_t bitWidth = 0;        // 0 = bukan bitfield
};

// Varian sum type. payloadType = InvalidType artinya varian tanpa payload
// (plain enum case). tagValue adalah nilai tag codegen-ready
// (bersumber dari SumType::Case::enumValue milik RhSemantic).
struct IrVariant {
    std::string name;
    TypeId payloadType = InvalidType;
    std::int64_t tagValue = 0;
};

// Type table milik IR sendiri, per IrModule (spesifikasi Bagian 5.1).
// Layout precomputed dan tersimpan eksplisit di sini — TIDAK dihitung
// ulang oleh backend (spesifikasi Bagian 5.6).
//
// Field yang relevan tergantung kind:
//   Int      -> isSigned, sizeBytes (lebar dalam byte)
//   Float    -> sizeBytes (4 = f32, 8 = f64)
//   Pointer  -> elementType (tipe pointee)
//   Array    -> elementType, arraySize
//   View     -> elementType
//   Struct   -> name, fields (dapat recursive via Pointer ke TypeId sendiri)
//   Sum      -> name, variants
//   FnPtr    -> paramTypes, returnType
struct IrType {
    IrTypeKind kind = IrTypeKind::Void;
    std::string name;                   // untuk named type (Struct/Sum)
    std::uint32_t sizeBytes = 0;
    std::uint32_t alignBytes = 0;
    bool isSigned = false;              // Int

    std::vector<IrField> fields;        // Struct
    std::vector<IrVariant> variants;    // Sum
    TypeId elementType = InvalidType;   // Pointer/Array/View
    std::uint64_t arraySize = 0;        // Array

    std::vector<TypeId> paramTypes;     // FnPtr
    TypeId returnType = InvalidType;    // FnPtr
};

} // namespace rhc
