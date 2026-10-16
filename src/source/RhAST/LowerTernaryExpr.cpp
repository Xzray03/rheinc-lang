#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Expr.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

#include <utility>

namespace rhc {

// Kondisi ? nilaiBenar : nilaiSalah -> if/else + phi di join.
// Prinsip: kondisi dievaluasi di block saat ini; tiap cabang
// dievaluasi di block sendiri; hasil = phi (then, else) di join;
// env digabung mergeAtJoin seperti statement if.
ValueId AstToIrTranslator::Impl::lowerTernaryExpr(const TernaryExpr* expr) {
    if (failed || builder.broken() || !blockOpen()) return InvalidValue;

    const NameValueMap preEnv = env.back().vars;

    const ValueId condition = lowerExpr(expr->condition);
    if (condition == InvalidValue) return InvalidValue;
    const TypeId resultType = mapType(
        semantic->context().getExprType(const_cast<TernaryExpr*>(expr)), expr);
    if (failed) return InvalidValue;

    const BlockId condBlock = currentBlock;
    const BlockId thenB = builder.createBlock(currentFunc, "ternary.then");
    const BlockId elseB = builder.createBlock(currentFunc, "ternary.else");
    const BlockId joinB = builder.createBlock(currentFunc, "ternary.join");
    builder.setTerminator(currentFunc, condBlock, IrOpcode::CondBr,
                          {IrOperand::value(condition),
                           IrOperand::block(thenB),
                           IrOperand::block(elseB)},
                          expr->range.begin);

    // Cabang then.
    setCurrent(thenB);
    const ValueId trueValue = lowerExpr(expr->trueValue);
    if (trueValue == InvalidValue) return InvalidValue;
    const NameValueMap thenEnv = env.back().vars;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                              {IrOperand::block(joinB)}, expr->range.begin);
    } else {
        return error(AstIrErrorCode::UnsupportedNode, expr,
                     "ternary dengan cabang then berakhir terminator "
                     "(kembalikan) belum didukung");
    }

    // Cabang else.
    env.back().vars = preEnv;
    setCurrent(elseB);
    const ValueId falseValue = lowerExpr(expr->falseValue);
    if (falseValue == InvalidValue) return InvalidValue;
    const NameValueMap elseEnv = env.back().vars;
    if (blockOpen()) {
        builder.setTerminator(currentFunc, currentBlock, IrOpcode::Br,
                              {IrOperand::block(joinB)}, expr->range.begin);
    } else {
        return error(AstIrErrorCode::UnsupportedNode, expr,
                     "ternary dengan cabang else berakhir terminator "
                     "(kembalikan) belum didukung");
    }

    // Hasil: phi [ then, trueValue ], [ else, falseValue ].
    const ValueId result = builder.createPhi(resultType, currentFunc, joinB,
                                             expr->range.begin);
    builder.addPhiIncoming(currentFunc, result, thenB, trueValue);
    builder.addPhiIncoming(currentFunc, result, elseB, falseValue);

    std::vector<std::pair<BlockId, const NameValueMap*>> preds;
    preds.emplace_back(thenB, &thenEnv);
    preds.emplace_back(elseB, &elseEnv);
    env.back().vars = mergeAtJoin(joinB, preds, preEnv);
    setCurrent(joinB);
    return result;
}

} // namespace rhc
