#include "AstToIrTranslatorImpl.h"

#include "RhParser/Decl.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/Type.h"

namespace rhc {

// Pemetaan semantic Type* -> IrTypeId (spesifikasi Bagian 5.1 & 5.5).
// TypeMapping (di IrModule) menyimpan hasilnya agar IR self-contained.
// Recursive struct: named type didaftarkan lebih dulu (forward reference),
// baru field-nya diisi — referensi rekursif kembali ke TypeId yang sama.
TypeId AstToIrTranslator::Impl::mapType(const Type* type,
                                        const AstNode* node) {
    if (!type) return InvalidType;

    const TypeId cached = module.typeMapping.lookup(type);
    if (cached != InvalidType) return cached;

    switch (type->kind()) {
    case TypeKind::Builtin: {
        // BuiltinObjectType ikut TypeKind::Builtin tapi bukan BuiltinType.
        if (const auto* object = dynamic_cast<const BuiltinObjectType*>(type)) {
            IrType t;
            t.kind = IrTypeKind::Struct;
            t.name = object->name;
            const TypeId id = builder.internType(t);
            module.typeMapping.map(type, id);
            for (const auto& [memberName, memberType] : object->members) {
                IrField f;
                f.name = memberName;
                f.type = mapType(memberType);
                module.types[id].fields.push_back(std::move(f));
            }
            return id;
        }
        const auto* builtin = static_cast<const BuiltinType*>(type);
        IrType t;
        switch (builtin->builtinKind) {
        case BuiltinTypeKind::Hampa:
            t.kind = IrTypeKind::Void;
            break;
        case BuiltinTypeKind::Logika:
            t.kind = IrTypeKind::Bool;
            t.sizeBytes = 1;
            t.alignBytes = 1;
            break;
        case BuiltinTypeKind::Desimal:
            t.kind = IrTypeKind::Float;
            t.sizeBytes = builtin->bits / 8;
            t.alignBytes = t.sizeBytes;
            break;
        case BuiltinTypeKind::Teks:
            // teks = memory-style buffer (Pilihan B, spesifikasi 3.4):
            // View dengan elemen byte. Detail repr (ptr+len) ke backend.
            t.kind = IrTypeKind::View;
            {
                IrType byte;
                byte.kind = IrTypeKind::Int;
                byte.sizeBytes = 1;
                byte.alignBytes = 1;
                byte.isSigned = false;
                t.elementType = builder.internType(byte);
            }
            break;
        default:  // Bilangan* & Karakter
            t.kind = IrTypeKind::Int;
            t.sizeBytes = builtin->bits / 8;
            t.alignBytes = t.sizeBytes;
            t.isSigned = builtin->isSigned;
            break;
        }
        const TypeId id = builder.internType(t);
        module.typeMapping.map(type, id);
        return id;
    }
    case TypeKind::Pointer: {
        const auto* pointer = static_cast<const PointerType*>(type);
        IrType t;
        t.kind = IrTypeKind::Pointer;
        t.elementType = pointer->pointee ? mapType(pointer->pointee)
                                         : InvalidType;
        t.sizeBytes = 8;
        t.alignBytes = 8;
        const TypeId id = builder.internType(t);
        module.typeMapping.map(type, id);
        return id;
    }
    case TypeKind::Array: {
        const auto* array = static_cast<const ArrayType*>(type);
        IrType t;
        t.kind = IrTypeKind::Array;
        t.elementType = mapType(array->elementType);
        t.arraySize = array->size.value_or(0);
        const TypeId id = builder.internType(t);
        module.typeMapping.map(type, id);
        return id;
    }
    case TypeKind::Slice: {
        const auto* slice = static_cast<const SliceType*>(type);
        IrType t;
        t.kind = IrTypeKind::View;
        t.elementType = mapType(slice->elementType);
        const TypeId id = builder.internType(t);
        module.typeMapping.map(type, id);
        return id;
    }
    case TypeKind::Function: {
        const auto* function = static_cast<const FunctionType*>(type);
        IrType t;
        t.kind = IrTypeKind::FnPtr;
        for (const FunctionParamType& param : function->params) {
            t.paramTypes.push_back(mapType(param.type));
        }
        if (function->returnType) {
            t.returnType = mapType(function->returnType);
        } else {
            IrType voidType;
            voidType.kind = IrTypeKind::Void;
            t.returnType = builder.internType(voidType);
        }
        const TypeId id = builder.internType(t);
        module.typeMapping.map(type, id);
        return id;
    }
    case TypeKind::Struct: {
        const auto* struktur = static_cast<const StructType*>(type);
        IrType t;
        t.kind = IrTypeKind::Struct;
        t.name = struktur->decl ? struktur->decl->name : std::string("<anon>");
        const TypeId id = builder.internType(t);
        // Daftarkan SEBELUM mengisi field: referensi rekursif (pointer ke
        // diri sendiri) resolve ke TypeId yang sama (spesifikasi 5.4).
        module.typeMapping.map(type, id);
        for (const Symbol* field : struktur->fields) {
            IrField f;
            f.name = field->name;
            f.type = mapType(field->type);
            // offsetBytes placeholder: aturan layout presisi dikunci
            // saat implementasi (spesifikasi Bagian 13).
            module.types[id].fields.push_back(std::move(f));
        }
        return id;
    }
    case TypeKind::Sum: {
        const auto* sum = static_cast<const SumType*>(type);
        IrType t;
        t.kind = IrTypeKind::Sum;
        t.name = sum->name;
        const TypeId id = builder.internType(t);
        module.typeMapping.map(type, id);
        for (const SumType::Case& kase : sum->cases) {
            IrVariant v;
            v.name = kase.name;
            v.tagValue = kase.hasEnumValue ? kase.enumValue : 0;
            if (!kase.fields.empty() && kase.fields[0].type) {
                v.payloadType = mapType(kase.fields[0].type);
            }
            module.types[id].variants.push_back(std::move(v));
        }
        return id;
    }
    case TypeKind::GenericInstantiation: {
        // Alias/instantiasi generik dengan basis BUILTIN (mis. "bilangan"
        // = bilangan bulat berparameter lebar): dipetakan ke builtin
        // dengan bits dari argumen generik pertama bertipe bilangan.
        // Basis struct generik (HashMap<K,V>, dsb) butuh substitusi
        // TypeVariable — belum diimplementasi (error jujur di bawah).
        const auto* inst = static_cast<const GenericInstantiationType*>(type);
        const Type* generic = inst->generic;
        if (generic && generic->kind() == TypeKind::Builtin) {
            const auto* gb = static_cast<const BuiltinType*>(generic);
            std::uint32_t bits = gb->bits;
            for (const auto& [argName, argType] : inst->arguments) {
                (void)argName;
                if (argType && argType->kind() == TypeKind::Builtin) {
                    const auto* argB =
                        static_cast<const BuiltinType*>(argType);
                    if (argB->builtinKind != BuiltinTypeKind::Teks &&
                        argB->builtinKind != BuiltinTypeKind::Logika &&
                        argB->builtinKind != BuiltinTypeKind::Hampa) {
                        bits = argB->bits;
                        break;
                    }
                }
            }
            IrType t;
            switch (gb->builtinKind) {
            case BuiltinTypeKind::Hampa:
                t.kind = IrTypeKind::Void;
                break;
            case BuiltinTypeKind::Logika:
                t.kind = IrTypeKind::Bool;
                t.sizeBytes = 1;
                t.alignBytes = 1;
                break;
            case BuiltinTypeKind::Desimal:
                t.kind = IrTypeKind::Float;
                t.sizeBytes = bits / 8;
                t.alignBytes = t.sizeBytes;
                break;
            case BuiltinTypeKind::Teks: {
                t.kind = IrTypeKind::View;
                IrType byte;
                byte.kind = IrTypeKind::Int;
                byte.sizeBytes = 1;
                byte.alignBytes = 1;
                byte.isSigned = false;
                t.elementType = builder.internType(byte);
                break;
            }
            default:
                t.kind = IrTypeKind::Int;
                t.sizeBytes = bits / 8;
                t.alignBytes = t.sizeBytes;
                t.isSigned = gb->isSigned;
                break;
            }
            const TypeId id = builder.internType(t);
            module.typeMapping.map(type, id);
            return id;
        }
        error(AstIrErrorCode::TypeMappingFailed, node,
              "instantiasi generik basis non-builtin (struct generik "
              "butuh substitusi tipe — belum diimplementasi)");
        return InvalidType;
    }
    case TypeKind::TypeVariable: {
        // Parameter generik TAKAK TERIKAT — terjadi saat alias generik
        // (mis. "bilangan<N>") dipakai tanpa argumen. Default ABI
        // internal: bilangan 64-bit bertanda (lihat DEVIATIONS.md D-2).
        IrType t;
        t.kind = IrTypeKind::Int;
        t.sizeBytes = 8;
        t.alignBytes = 8;
        t.isSigned = true;
        const TypeId id = builder.internType(t);
        module.typeMapping.map(type, id);
        return id;
    }
    case TypeKind::Error:
        error(AstIrErrorCode::TypeMappingFailed, node,
              std::string("tipe semantic Error tidak dapat dipetakan ke IR"
                          " (loc ") +
                  std::to_string(node ? node->range.begin : 0) + ")");
        return InvalidType;
    }
    error(AstIrErrorCode::TypeMappingFailed, nullptr,
          "TypeKind tidak dikenal");
    return InvalidType;
}

} // namespace rhc
