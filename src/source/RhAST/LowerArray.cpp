#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

#include <functional>
#include <utility>
#include <vector>

namespace rhc {

// Literal larik -> construct chain (spesifikasi Bagian 6.3, bentuk
// non-blob): Construct [TypeV, elem0, elem1, ...]. Elemen rekursif
// (larik bersarang = construct bertingkat).
ValueId AstToIrTranslator::Impl::lowerArrayLiteral(
        const ArrayLiteralExpr* expr) {
    const TypeId arrayType = mapType(
        semantic->context().getExprType(const_cast<ArrayLiteralExpr*>(expr)),
        expr);
    if (failed || arrayType == InvalidType) return InvalidValue;

    std::vector<IrOperand> operands;
    operands.push_back(IrOperand::type(arrayType));
    for (const ExprNode* element : expr->elements) {
        const ValueId value = lowerExpr(element);
        if (value == InvalidValue) return InvalidValue;
        operands.push_back(IrOperand::value(value));
    }
    return builder.emit(IrOpcode::Construct, arrayType, std::move(operands),
                        expr->range.begin);
}

// Baca elemen: obj[idx] -> Extract. Index boleh konstanta (ImmV) atau
// nilai dinamis (ValueV) — mis. larik[i] dalam perulangan.
ValueId AstToIrTranslator::Impl::lowerArrayIndexRead(
        const ArrayIndexExpr* expr) {
    const ValueId object = lowerExpr(expr->object);
    if (object == InvalidValue) return InvalidValue;
    const ValueId index = lowerExpr(expr->index);
    if (index == InvalidValue) return InvalidValue;
    const TypeId resultType = mapType(
        semantic->context().getExprType(const_cast<ArrayIndexExpr*>(expr)),
        expr);
    if (failed) return InvalidValue;
    return builder.emit(IrOpcode::Extract, resultType,
                        {IrOperand::value(object), IrOperand::value(index)},
                        expr->range.begin);
}

// Tulis elemen root by-value (a[i] = v): Insert persistent + rebind
// root (versioning SSA). Root harus identifier; index dinamis didukung.
ValueId AstToIrTranslator::Impl::lowerArrayIndexWrite(
        const ArrayIndexExpr* target, const ExprNode* valueExpr) {
    const ValueId value = lowerExpr(valueExpr);
    if (value == InvalidValue) return InvalidValue;
    return lowerArrayIndexWriteValue(target, value);
}

ValueId AstToIrTranslator::Impl::lowerArrayIndexWriteValue(
        const ArrayIndexExpr* target, ValueId value) {
    // Kumpulkan path indeks dari luar ke dalam.
    std::vector<const ExprNode*> indexExprs;
    const ExprNode* base = target;
    while (base && base->kind == AstKind::ArrayIndexExpr) {
        const auto* idx = static_cast<const ArrayIndexExpr*>(base);
        indexExprs.push_back(idx->index);
        base = idx->object;
    }
    if (!base || base->kind != AstKind::IdentifierExpr) {
        error(AstIrErrorCode::UnsupportedNode, target,
              "assignment elemen larik dengan root non-identifier "
              "(campuran member/index atau deref) belum didukung");
        return InvalidValue;
    }
    const auto* root = static_cast<const IdentifierExpr*>(base);
    const ValueId rootValue = lookupRaw(root->name);
    if (rootValue == InvalidValue) {
        error(AstIrErrorCode::MalformedShape, target,
              "assignment elemen '" + root->name + "' yang belum "
              "dideklarasikan");
        return InvalidValue;
    }
    const TypeId rootType =
        builder.function(currentFunc).values[rootValue].type;

    std::function<ValueId(ValueId, std::size_t)> writeAt =
        [&](ValueId current, std::size_t level) -> ValueId {
        const ValueId idx = lowerExpr(indexExprs[indexExprs.size() - 1 - level]);
        if (idx == InvalidValue) return InvalidValue;
        if (level + 1 == indexExprs.size()) {
            return builder.emit(IrOpcode::Insert, rootType,
                                {IrOperand::value(current),
                                 IrOperand::value(idx),
                                 IrOperand::value(value)},
                                target->range.begin);
        }
        // Extract level perantara HARUS punya resultType valid agar SSA
        // value teralokasi (IrBuilder::emit hanya mendaftarkan value bila
        // resultType != InvalidType). Operand InvalidValue/%v? yang lolos
        // ke adopt ditolak RhIR (operand id di luar tabel terkait).
        const TypeId currentType =
            builder.function(currentFunc).values[current].type;
        TypeId elemType = InvalidType;
        if (currentType != InvalidType &&
            module.types[currentType].kind == IrTypeKind::Array) {
            elemType = module.types[currentType].elementType;
        }
        if (elemType == InvalidType) {
            error(AstIrErrorCode::MalformedShape, target,
                  "assignment elemen larik bersarang: tipe elemen perantara "
                  "tidak dapat ditentukan");
            return InvalidValue;
        }
        const ValueId inner = builder.emit(
            IrOpcode::Extract, elemType,
            {IrOperand::value(current), IrOperand::value(idx)},
            target->range.begin);
        const ValueId newInner = writeAt(inner, level + 1);
        if (newInner == InvalidValue) return InvalidValue;
        return builder.emit(IrOpcode::Insert, rootType,
                            {IrOperand::value(current),
                             IrOperand::value(idx),
                             IrOperand::value(newInner)},
                            target->range.begin);
    };
    const ValueId updated = writeAt(rootValue, 0);
    if (updated == InvalidValue) return InvalidValue;
    bindLocal(root->name, updated);
    return value;
}

} // namespace rhc
