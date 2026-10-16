#include "AstToIrTranslatorImpl.h"

#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

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
    if (target->object->kind != AstKind::IdentifierExpr) {
        error(AstIrErrorCode::UnsupportedNode, target,
              "assignment elemen larik dengan root non-identifier "
              "(bersarang/deref) belum didukung");
        return InvalidValue;
    }
    const auto* root = static_cast<const IdentifierExpr*>(target->object);
    const ValueId rootValue = lookupRaw(root->name);
    if (rootValue == InvalidValue) {
        error(AstIrErrorCode::MalformedShape, target,
              "assignment elemen '" + root->name + "' yang belum "
              "dideklarasikan");
        return InvalidValue;
    }
    const ValueId index = lowerExpr(target->index);
    if (index == InvalidValue) return InvalidValue;

    const TypeId rootType =
        builder.function(currentFunc).values[rootValue].type;
    const ValueId updated = builder.emit(
        IrOpcode::Insert, rootType,
        {IrOperand::value(rootValue), IrOperand::value(index),
         IrOperand::value(value)},
        target->range.begin);
    if (updated == InvalidValue) return InvalidValue;
    bindLocal(root->name, updated);
    return value;
}

} // namespace rhc
