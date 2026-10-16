#include "AstToIrTranslatorImpl.h"

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
    const StructType* container;
};

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
        if (!containerType || containerType->kind() != TypeKind::Struct) {
            return false;
        }
        const auto* struktur = static_cast<const StructType*>(containerType);
        bool found = false;
        for (std::size_t i = 0; i < struktur->fields.size(); ++i) {
            if (struktur->fields[i]->name == it->first) {
                steps.push_back({it->first, static_cast<std::uint32_t>(i),
                                 struktur});
                containerType = struktur->fields[i]->type;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return !steps.empty();
}

} // namespace

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
            return builder.emit(IrOpcode::ConstFloat, typeId,
                                {IrOperand::imm(0)}, loc);
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
    error(AstIrErrorCode::UnsupportedNode, nullptr,
          "default value untuk tipe ini belum didukung (array/view/sum)");
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
                mapType(step.container->fields[step.index]->type);
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
        const TypeId fieldType = mapType(step.container->fields[step.index]->type,
            step.container->fields[step.index]->decl);
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
    const IdentifierExpr* root = nullptr;
    std::vector<MemberStep> steps;
    if (!collectPath(target, root, steps, semantic->context())) {
        error(AstIrErrorCode::UnsupportedNode, target,
              "assignment member dengan root non-identifier belum didukung");
        return InvalidValue;
    }
    const ValueId value = lowerExpr(valueExpr);
    if (value == InvalidValue) return InvalidValue;

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
                mapType(steps[i].container->fields[steps[i].index]->type);
            addr = builder.emit(IrOpcode::FieldAddr, pointerTo(fieldType),
                                {IrOperand::value(addr),
                                 IrOperand::type(mapType(steps[i].container)),
                                 IrOperand::imm(steps[i].index)},
                                target->range.begin);
        }
        const MemberStep& last = steps.back();
        const TypeId lastFieldType =
            mapType(last.container->fields[last.index]->type);
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
        const TypeId fieldType = mapType(step.container->fields[step.index]->type,
            step.container->fields[step.index]->decl);
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
