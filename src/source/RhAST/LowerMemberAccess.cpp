#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/Type.h"

#include <functional>
#include <utility>
#include <vector>

namespace rhc {

namespace {

// Satu langkah akses member: nama field + index + tipe kontainer.
struct MemberStep {
    std::string name;
    std::uint32_t index;
    const Type* container;   // StructType atau BuiltinObjectType
};

// Tipe field pada step: struct fields ATAU members object builtin
// (non-function). Indeks mengikuti urutan yang sama dengan collectPath.
TypeId memberFieldType(AstToIrTranslator::Impl& self,
                       const MemberStep& step) {
    if (const auto* struktur =
            dynamic_cast<const StructType*>(step.container)) {
        return self.mapType(struktur->fields[step.index]->type,
                            struktur->fields[step.index]->decl);
    }
    const auto* object = static_cast<const BuiltinObjectType*>(step.container);
    std::uint32_t i = 0;
    for (const auto& [memberName, type] : object->members) {
        (void)memberName;
        if (type->kind() == TypeKind::Function) continue;
        if (i == step.index) return self.mapType(type, nullptr);
        ++i;
    }
    return InvalidType;
}

// Kumpulkan path member dari root IdentifierExpr.
// a.b.c  -> root=a, steps=[b, c];  p->x->y -> root=p, steps=[x, y].
// Mengembalikan false bila bentuk tidak didukung (root bukan identifier
// atau ada CallExpr di tengah path).
bool collectPath(const ExprNode* expr, const IdentifierExpr*& root,
                 std::vector<MemberStep>& steps,
                 const SemanticContext& context) {
    std::vector<std::pair<std::string, const ExprNode*>> reversed;
    const ExprNode* current = expr;
    while (current && current->kind == AstKind::MemberExpr) {
        const auto* member = static_cast<const MemberExpr*>(current);
        if (member->memberName.empty()) return false;
        reversed.emplace_back(member->memberName, member->object);
        current = member->object;
    }
    if (!current || current->kind != AstKind::IdentifierExpr) return false;
    root = static_cast<const IdentifierExpr*>(current);

    // Telusuri tipe dari root untuk mengisi index + container tiap step.
    const Type* containerType = context.getExprType(root);
    for (auto it = reversed.rbegin(); it != reversed.rend(); ++it) {
        // Dereference pointer di tengah path.
        if (containerType && containerType->kind() == TypeKind::Pointer) {
            containerType = static_cast<const PointerType*>(containerType)->pointee;
        }
        // Resolve member: fields struct, instantiasi struct generik,
        // ATAU members object builtin (non-function = fields).
        if (!containerType) return false;
        const Type* memberType = nullptr;
        std::uint32_t memberIndex = 0;
        bool found = false;
        const Type* structLike = containerType;
        if (const auto* inst =
                dynamic_cast<const GenericInstantiationType*>(containerType)) {
            structLike = inst->generic;
        }
        if (const auto* struktur =
                dynamic_cast<const StructType*>(structLike)) {
            for (std::size_t i = 0; i < struktur->fields.size(); ++i) {
                if (struktur->fields[i]->name == it->first) {
                    memberType = struktur->fields[i]->type;
                    memberIndex = static_cast<std::uint32_t>(i);
                    found = true;
                    break;
                }
            }
        } else if (const auto* object =
                       dynamic_cast<const BuiltinObjectType*>(containerType)) {
            std::uint32_t i = 0;
            for (const auto& [memberName, type] : object->members) {
                if (type->kind() == TypeKind::Function) {
                    ++i;
                    continue;  // method, bukan field
                }
                if (memberName == it->first) {
                    memberType = type;
                    memberIndex = i;
                    found = true;
                    break;
                }
                ++i;
            }
        }
        if (!found || !memberType) return false;
        steps.push_back({it->first, memberIndex, structLike});
        containerType = memberType;
    }
    return !steps.empty();
}

} // namespace

// Default value murni dari struktur IrType (dipakai untuk tipe hasil
// substitusi generik, di mana semantic type tidak lagi representatif).
ValueId emitDefaultFromIrType(AstToIrTranslator::Impl& self,
                              TypeId typeId, SourceLocation loc) {
    const IrType t = self.module.types[typeId];  // by value: anti-UAF realloc
    switch (t.kind) {
    case IrTypeKind::Bool:
        return self.builder.emit(IrOpcode::ConstBool, typeId,
                            {IrOperand::imm(0)}, loc);
    case IrTypeKind::Int: {
        std::vector<IrOperand> ops;
        ops.push_back(IrOperand::imm(0));
        if (t.sizeBytes == 8) ops.push_back(IrOperand::imm(0));
        return self.builder.emit(IrOpcode::ConstInt, typeId, std::move(ops), loc);
    }
    case IrTypeKind::Float: {
        // desimal = double 64-bit SELALU: 2 ImmV (lo, hi).
        std::vector<IrOperand> ops;
        ops.push_back(IrOperand::imm(0));
        ops.push_back(IrOperand::imm(0));
        return self.builder.emit(IrOpcode::ConstFloat, typeId, std::move(ops), loc);
    }
    case IrTypeKind::Pointer:
    case IrTypeKind::FnPtr:
        // FnPtr default = null pointer (object method table kosong).
        return self.builder.emit(IrOpcode::ConstNull, typeId, {}, loc);
    case IrTypeKind::View: {
        const ConstId empty = self.encodeStringLiteral("", typeId, nullptr);
        return self.builder.emit(IrOpcode::ConstString, typeId,
                            {IrOperand::constant(empty)}, loc);
    }
    case IrTypeKind::Array:
    case IrTypeKind::Struct: {
        std::vector<IrOperand> operands;
        operands.push_back(IrOperand::type(typeId));
        const std::size_t count = t.kind == IrTypeKind::Array
            ? static_cast<std::size_t>(t.arraySize)
            : t.fields.size();
        for (std::size_t i = 0; i < count; ++i) {
            const TypeId fieldType = t.kind == IrTypeKind::Array
                ? t.elementType : t.fields[i].type;
            const ValueId v = emitDefaultFromIrType(self, fieldType, loc);
            if (v == InvalidValue) return InvalidValue;
            operands.push_back(IrOperand::value(v));
        }
        return self.builder.emit(IrOpcode::Construct, typeId,
                            std::move(operands), loc);
    }
    case IrTypeKind::Sum: {
        // Default sum: varian pertama + payload default bila ada.
        if (t.variants.empty()) {
            self.error(AstIrErrorCode::UnsupportedNode, nullptr,
                       "default value sum tanpa varian");
            return InvalidValue;
        }
        std::vector<IrOperand> operands;
        operands.push_back(IrOperand::type(typeId));
        operands.push_back(IrOperand::imm(
            static_cast<std::uint32_t>(t.variants.front().tagValue)));
        if (t.variants.front().payloadType != InvalidType) {
            const ValueId payload =
                emitDefaultFromIrType(self, t.variants.front().payloadType, loc);
            if (payload == InvalidValue) return InvalidValue;
            operands.push_back(IrOperand::value(payload));
        }
        return self.builder.emit(IrOpcode::Construct, typeId,
                                 std::move(operands), loc);
    }
    default:
        self.error(AstIrErrorCode::UnsupportedNode, nullptr,
              "default value IR kind=" +
              std::to_string(static_cast<int>(t.kind)) + " belum didukung");
        return InvalidValue;
    }
}

// Default value (zero/empty) sesuai prinsip verbose IR: Construct
// eksplisit dengan tiap field diisi konstanta zero yang sesuai tipe.
ValueId AstToIrTranslator::Impl::emitDefaultValue(TypeId typeId,
                                                  const Type* type,
                                                  SourceLocation loc) {
    if (!type) return InvalidValue;
    if (type->kind() == TypeKind::Builtin) {
        const auto* builtin = static_cast<const BuiltinType*>(type);
        switch (builtin->builtinKind) {
        case BuiltinTypeKind::Hampa:
            return InvalidValue;  // tidak menghasilkan value
        case BuiltinTypeKind::Logika:
            return builder.emit(IrOpcode::ConstBool, typeId,
                                {IrOperand::imm(0)}, loc);
        case BuiltinTypeKind::Desimal:
            // desimal = double 64-bit SELALU: 2 ImmV (lo, hi).
            return builder.emit(IrOpcode::ConstFloat, typeId,
                                {IrOperand::imm(0), IrOperand::imm(0)}, loc);
        case BuiltinTypeKind::Teks: {
            const ConstId empty = encodeStringLiteral("", typeId, nullptr);
            return builder.emit(IrOpcode::ConstString, typeId,
                                {IrOperand::constant(empty)}, loc);
        }
        default:
            return builder.emit(IrOpcode::ConstInt, typeId,
                                {IrOperand::imm(0)}, loc);
        }
    }
    if (type->kind() == TypeKind::Pointer) {
        return builder.emit(IrOpcode::ConstNull, typeId, {}, loc);
    }
    if (type->kind() == TypeKind::Struct) {
        const auto* struktur = static_cast<const StructType*>(type);
        std::vector<IrOperand> operands;
        operands.push_back(IrOperand::type(typeId));
        for (const Symbol* field : struktur->fields) {
            const TypeId fieldType = mapType(field->type, field->decl);
            const ValueId v = emitDefaultValue(fieldType, field->type, loc);
            if (v == InvalidValue) return InvalidValue;
            operands.push_back(IrOperand::value(v));
        }
        return builder.emit(IrOpcode::Construct, typeId, std::move(operands),
                            loc);
    }
    if (type->kind() == TypeKind::GenericInstantiation) {
        // Tipe hasil substitusi generik: bangun default dari struktur
        // IrType yang sudah ter-map (fields terkonkretisasi).
        return emitDefaultFromIrType(*this, typeId, loc);
    }
    if (type->kind() == TypeKind::Array) {
        const auto* array = static_cast<const ArrayType*>(type);
        std::vector<IrOperand> operands;
        operands.push_back(IrOperand::type(typeId));
        for (std::uint64_t i = 0; i < array->size.value_or(0); ++i) {
            const ValueId v = emitDefaultValue(
                module.types[typeId].elementType, array->elementType, loc);
            if (v == InvalidValue) return InvalidValue;
            operands.push_back(IrOperand::value(v));
        }
        return builder.emit(IrOpcode::Construct, typeId, std::move(operands),
                            loc);
    }
    error(AstIrErrorCode::UnsupportedNode, nullptr,
          "default value untuk tipe semantic kind=" +
          std::to_string(static_cast<int>(type->kind())) +
          " belum didukung");
    return InvalidValue;
}

// Baca member: dua jalur sesuai aliasability (spesifikasi Bagian 3):
//   - root memory-style/pointer -> FieldAddr chain + Load
//   - root by-value SSA          -> Extract chain (value persisten)
ValueId AstToIrTranslator::Impl::lowerMemberRead(const ExprNode* expr) {
    const IdentifierExpr* root = nullptr;
    std::vector<MemberStep> steps;
    if (!collectPath(expr, root, steps, semantic->context())) {
        error(AstIrErrorCode::UnsupportedNode, expr,
              "aksés member dengan root non-identifier/chain kompleks "
              "belum didukung");
        return InvalidValue;
    }

    const Type* rootType = semantic->context().getExprType(
        const_cast<IdentifierExpr*>(root));
    const bool memoryPath =
        isMemoryStyle(root->name) ||
        (rootType && rootType->kind() == TypeKind::Pointer);

    if (memoryPath) {
        // Basis alamat: ptr Alloc (memory-style) atau SSA pointer value.
        ValueId addr = lookupRaw(root->name);
        if (addr == InvalidValue) return InvalidValue;
        for (const MemberStep& step : steps) {
            const TypeId fieldType =
                memberFieldType(*this, step);
            addr = builder.emit(IrOpcode::FieldAddr, pointerTo(fieldType),
                                {IrOperand::value(addr),
                                 IrOperand::type(mapType(step.container)),
                                 IrOperand::imm(step.index)},
                                expr->range.begin);
        }
        const Type* resultType = semantic->context().getExprType(
            const_cast<ExprNode*>(expr));
        return builder.emit(IrOpcode::Load, mapType(resultType),
                            {IrOperand::value(addr)}, expr->range.begin,
                            steps.back().name);
    }

    // Jalur SSA by-value.
    ValueId current = lookupLocal(root->name);
    if (current == InvalidValue) return InvalidValue;
    for (const MemberStep& step : steps) {
        TypeId fieldType = memberFieldType(*this, step);
        // Field generik T (TypeVariable) -> mapType default bilangan
        // (DEVIATIONS D-2) padahal instansiasi konkret (Pasangan(teks)
        // -> field View). Resolve dari IR container type TERINSTANISIASI
        // (tipe value SSA `current`) — extract resultType benar ->
        // print/home/slot semua benar (generik.rhc).
        {
            TypeId curTy = InvalidType;
            const auto& cv = builder.function(currentFunc).values[current];
            if (cv.kind == IrValue::Kind::Instr &&
                cv.payload < builder.function(currentFunc).instrs.size()) {
                curTy = builder.function(currentFunc)
                            .instrs[cv.payload].resultType;
            } else {
                curTy = cv.type;
            }
            if (curTy != InvalidType && curTy < module.types.size() &&
                module.types[curTy].kind == IrTypeKind::Struct &&
                step.index < module.types[curTy].fields.size()) {
                const TypeId f2 =
                    module.types[curTy].fields[step.index].type;
                if (f2 != InvalidType) fieldType = f2;
            }
        }
        current = builder.emit(IrOpcode::Extract, fieldType,
                               {IrOperand::value(current),
                                IrOperand::imm(step.index)},
                               expr->range.begin, step.name);
    }
    return current;
}

// Tulis member (persistent update):
//   - memory path -> FieldAddr chain + Store (nilai lama berubah)
//   - by-value    -> Insert chain, root di-versioning ulang (§3.2)
ValueId AstToIrTranslator::Impl::lowerMemberWrite(const ExprNode* target,
                                                  const ExprNode* valueExpr) {
    const ValueId value = lowerExpr(valueExpr);
    if (value == InvalidValue) return InvalidValue;
    return lowerMemberWriteValue(target, value);
}

ValueId AstToIrTranslator::Impl::lowerMemberWriteValue(
        const ExprNode* target, ValueId value) {
    const IdentifierExpr* root = nullptr;
    std::vector<MemberStep> steps;
    if (!collectPath(target, root, steps, semantic->context())) {
        error(AstIrErrorCode::UnsupportedNode, target,
              "assignment member dengan root non-identifier belum didukung");
        return InvalidValue;
    }

    const Type* rootType = semantic->context().getExprType(
        const_cast<IdentifierExpr*>(root));
    const bool memoryPath =
        isMemoryStyle(root->name) ||
        (rootType && rootType->kind() == TypeKind::Pointer);

    if (memoryPath) {
        ValueId addr = lookupRaw(root->name);
        if (addr == InvalidValue) return InvalidValue;
        for (std::size_t i = 0; i + 1 < steps.size(); ++i) {
            const TypeId fieldType =
                memberFieldType(*this, steps[i]);
            addr = builder.emit(IrOpcode::FieldAddr, pointerTo(fieldType),
                                {IrOperand::value(addr),
                                 IrOperand::type(mapType(steps[i].container)),
                                 IrOperand::imm(steps[i].index)},
                                target->range.begin);
        }
        const MemberStep& last = steps.back();
        const TypeId lastFieldType = memberFieldType(*this, last);
        addr = builder.emit(IrOpcode::FieldAddr, pointerTo(lastFieldType),
                            {IrOperand::value(addr),
                             IrOperand::type(mapType(last.container)),
                             IrOperand::imm(last.index)},
                            target->range.begin);
        builder.emit(IrOpcode::Store, InvalidType,
                     {IrOperand::value(addr), IrOperand::value(value)},
                     target->range.begin);
        return builder.broken() ? InvalidValue : value;
    }

    // Jalur SSA: bangun Insert chain dari dalam ke luar.
    if (lookupLocal(root->name) == InvalidValue) return InvalidValue;

    // Rekursi: Insert chain dibangun dari dalam ke luar.
    std::function<ValueId(ValueId, std::size_t)> insertAt =
        [&](ValueId currentBase, std::size_t i) -> ValueId {
        const MemberStep& step = steps[i];
        const TypeId fieldType = memberFieldType(*this, step);
        if (i + 1 == steps.size()) {
            return builder.emit(IrOpcode::Insert, mapType(rootType),
                                {IrOperand::value(currentBase),
                                 IrOperand::imm(step.index),
                                 IrOperand::value(value)},
                                target->range.begin);
        }
        const ValueId inner = builder.emit(
            IrOpcode::Extract, fieldType,
            {IrOperand::value(currentBase), IrOperand::imm(step.index)},
            target->range.begin);
        const ValueId newInner = insertAt(inner, i + 1);
        return builder.emit(IrOpcode::Insert, mapType(rootType),
                            {IrOperand::value(currentBase),
                             IrOperand::imm(step.index),
                             IrOperand::value(newInner)},
                            target->range.begin);
    };
    const ValueId newRoot = insertAt(lookupLocal(root->name), 0);
    if (newRoot == InvalidValue) return InvalidValue;
    bindLocal(root->name, newRoot);
    return value;
}

} // namespace rhc
