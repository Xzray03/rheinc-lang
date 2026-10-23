#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhLexer/TokenKind.h"
#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/Type.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace rhc {

namespace {

// Salinan findSumVariant dari LowerExpr.cpp. Karena namespace
// anonim punya internal linkage, duplikasi ini legal; TODO saat
// refactor Opsi B: konsolidasikan helper bersama.
// Cari varian sum bernama `caseName` pada tipe sum IR.
// Mengembalikan indeks varian atau -1.
int findSumVariant(const IrModule& module, TypeId sumType,
                   const std::string& caseName) {
    if (sumType >= module.types.size() ||
        module.types[sumType].kind != IrTypeKind::Sum) {
        return -1;
    }
    const IrType t = module.types[sumType];  // by value: anti-UAF realloc
    for (std::size_t i = 0; i < t.variants.size(); ++i) {
        if (t.variants[i].name == caseName) return static_cast<int>(i);
    }
    return -1;
}

} // namespace

// a[start sampai end langkah step] -> SliceView + Construct Opsi:ADA(view).
// Batasan fase: validasi batas (kasus KOSONG saat di luar rentang) belum
// di-lowering; bounds-check runtime adalah fase berikutnya.
// a[s1] -> buildSliceView + wrap Opsi:ADA.
ValueId AstToIrTranslator::Impl::lowerSliceFrom(
        const ExprNode* objectExpr, const SliceExpr* node,
        const ExprNode* locExpr) {
    const ValueId source = lowerExpr(objectExpr);
    if (source == InvalidValue) return InvalidValue;
    return wrapSliceInOpsi(buildSliceView(source, node, locExpr), locExpr);
}

// m[s1, s2, ...] (multi-dim): SliceView berurutan per dimensi, hasil
// akhir dibungkus Opsi:ADA sekali. Tipe payload IR = view terakhir;
// tipe semantic Opsi<elementType> — selisih dicatat di DEVIATIONS.
ValueId AstToIrTranslator::Impl::lowerSliceMulti(
        const ExprNode* objectExpr, const ArrayLiteralExpr* slices,
        const ExprNode* locExpr) {
    ValueId current = lowerExpr(objectExpr);
    if (current == InvalidValue) return InvalidValue;
    for (const ExprNode* element : slices->elements) {
        if (element->kind != AstKind::SliceExpr) {
            error(AstIrErrorCode::UnsupportedNode, element,
                  "multi-dim slice dengan elemen non-slice belum didukung");
            return InvalidValue;
        }
        current = buildSliceView(current,
                                 static_cast<const SliceExpr*>(element),
                                 locExpr);
        if (current == InvalidValue) return InvalidValue;
    }
    return wrapSliceInOpsi(current, locExpr);
}

// Bangun satu level SliceView dari source + batas slice (default:
// 0 .. Len .. 1). Tidak membungkus Opsi — itu tugas caller.
ValueId AstToIrTranslator::Impl::buildSliceView(
        ValueId source, const SliceExpr* node,
        const ExprNode* locExpr) {
    TypeId sourceType =
        builder.function(currentFunc).values[source].type;
    const IrType* srcT = &module.types[sourceType];
    if (srcT->kind == IrTypeKind::Struct) {
        // Koleksi object (mis. Daftar<T>): konversi ke larik dulu via
        // <nama object huruf kecil>_ke_larik, lalu slice hasilnya.
        std::string convName = srcT->name;
        for (char& ch : convName) {
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
        }
        convName += "_ke_larik";
        const ValueId converted =
            emitCallByName(convName, {source}, locExpr->range.begin);
        if (converted == InvalidValue) return InvalidValue;
        sourceType = builder.function(currentFunc).values[converted].type;
        srcT = &module.types[sourceType];
        // sumber slice menjadi hasil konversi
        const ValueId zero0 = builder.emit(IrOpcode::ConstInt, sourceType,
                                           {IrOperand::imm(0)},
                                           locExpr->range.begin);
        (void)zero0;
        // lanjut dengan source yang dikonversi: ganti operand pertama
        // dengan membangun ulang via recursion sederhana.
        return buildSliceView(converted, node, locExpr);
    }
    if (srcT->kind != IrTypeKind::Array && srcT->kind != IrTypeKind::View) {
        error(AstIrErrorCode::UnsupportedNode, locExpr,
              "slice pada sumber non-larik/non-view belum didukung");
        return InvalidValue;
    }
    IrType v;
    v.kind = IrTypeKind::View;
    v.elementType = srcT->elementType;
    const TypeId viewType = builder.internType(v);

    const ValueId zero = builder.emit(IrOpcode::ConstInt, viewType,
                                      {IrOperand::imm(0)},
                                      locExpr->range.begin);
    const ValueId start = node->start ? lowerExpr(node->start) : zero;
    if (start == InvalidValue) return InvalidValue;
    const ValueId limit = builder.emit(IrOpcode::Len, viewType,
                                       {IrOperand::value(source)},
                                       locExpr->range.begin);
    const ValueId end = node->end ? lowerExpr(node->end) : limit;
    if (end == InvalidValue) return InvalidValue;
    const ValueId one = builder.emit(IrOpcode::ConstInt, viewType,
                                     {IrOperand::imm(1)},
                                     locExpr->range.begin);
    const ValueId step = node->step ? lowerExpr(node->step) : one;
    if (step == InvalidValue) return InvalidValue;
    return builder.emit(
        IrOpcode::SliceView, viewType,
        {IrOperand::value(source), IrOperand::value(start),
         IrOperand::value(end), IrOperand::value(step)},
        locExpr->range.begin);
}

// Bungkus view dalam Construct Opsi:ADA sesuai tipe hasil semantic.
ValueId AstToIrTranslator::Impl::wrapSliceInOpsi(ValueId view,
                                                 const ExprNode* locExpr) {
    if (view == InvalidValue) return InvalidValue;
    const TypeId resultType = mapType(
        semantic->context().getExprType(const_cast<ExprNode*>(locExpr)),
        locExpr);
    if (resultType != InvalidType &&
        module.types[resultType].kind == IrTypeKind::Sum) {
        const int ada = findSumVariant(module, resultType, "ADA");
        if (ada >= 0) {
            std::vector<IrOperand> operands;
            operands.push_back(IrOperand::type(resultType));
            operands.push_back(IrOperand::imm(static_cast<std::uint32_t>(
                module.types[resultType].variants[ada].tagValue)));
            operands.push_back(IrOperand::value(view));
            return builder.emit(IrOpcode::Construct, resultType,
                                std::move(operands), locExpr->range.begin);
        }
    }
    return view;
}


// Ranged write: a[m sampai n] = [e0, e1, ...] -> Insert berurutan per
// elemen (idx = start + i) + rebind root. RHS harus literal larik pada
// fase ini (rentang dinamis = fase berikutnya).
ValueId AstToIrTranslator::Impl::lowerSliceWrite(
        const ArrayIndexExpr* target, const SliceExpr* slice,
        const ExprNode* valueExpr) {
    if (target->object->kind != AstKind::IdentifierExpr) {
        error(AstIrErrorCode::UnsupportedNode, target,
              "ranged write dengan root non-identifier belum didukung");
        return InvalidValue;
    }
    if (valueExpr->kind != AstKind::ArrayLiteralExpr) {
        error(AstIrErrorCode::UnsupportedNode, valueExpr,
              "ranged write memerlukan literal larik sebagai RHS "
              "(fase ini)");
        return InvalidValue;
    }
    const auto* root = static_cast<const IdentifierExpr*>(target->object);
    const ValueId rootValue = lookupRaw(root->name);
    if (rootValue == InvalidValue) {
        error(AstIrErrorCode::MalformedShape, target,
              "ranged write ke '" + root->name + "' yang belum dideklarasikan");
        return InvalidValue;
    }
    const auto* literal = static_cast<const ArrayLiteralExpr*>(valueExpr);

    const TypeId rootType =
        builder.function(currentFunc).values[rootValue].type;
    const ValueId zero = builder.emit(IrOpcode::ConstInt, rootType,
                                      {IrOperand::imm(0)},
                                      target->range.begin);
    const ValueId start = slice->start ? lowerExpr(slice->start) : zero;
    if (start == InvalidValue) return InvalidValue;

    ValueId current = rootValue;
    for (std::size_t i = 0; i < literal->elements.size(); ++i) {
        const ValueId offset = builder.emit(
            IrOpcode::ConstInt, rootType,
            {IrOperand::imm(static_cast<std::uint32_t>(i))},
            target->range.begin);
        const ValueId idx = builder.emit(
            IrOpcode::Add, rootType,
            {IrOperand::value(start), IrOperand::value(offset)},
            target->range.begin);
        const ValueId elem = lowerExpr(literal->elements[i]);
        if (elem == InvalidValue) return InvalidValue;
        current = builder.emit(IrOpcode::Insert, rootType,
                               {IrOperand::value(current),
                                IrOperand::value(idx),
                                IrOperand::value(elem)},
                               target->range.begin);
        if (current == InvalidValue) return InvalidValue;
    }
    bindLocal(root->name, current);
    return current;
}

} // namespace rhc
