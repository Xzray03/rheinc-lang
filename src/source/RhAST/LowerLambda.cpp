#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Expr.h"
#include "RhParser/Stmt.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Type.h"

#include <unordered_set>
#include <utility>
#include <vector>

namespace rhc {

// Lambda -> fungsi unik "lambda.N" dengan capture sebagai parameter
// implisit (urutan deterministik = urutan kemunculan bebas pertama).
// Nilai lambda di call site = AddrOf @fn (fn ptr); pemanggilan lewat
// CallIndirect (lihat LowerExpr).
FuncId AstToIrTranslator::Impl::lowerLambda(const LambdaExpr* expr) {
    // Kumpulkan capture: identifier bebas yang resolve di scope pembuat.
    std::vector<std::string> freeVars;
    {
        std::vector<std::string> all;
        for (const StmtNode* st : expr->body->statements) {
            collectStmtIdentifiers(st, all);
        }
        std::unordered_set<std::string> params;
        for (const auto& [pname, ptype] : expr->parameters) {
            (void)ptype;
            params.insert(pname);
        }
        std::unordered_set<std::string> seen;
        for (const std::string& name : all) {
            if (params.count(name) || seen.count(name)) continue;
            if (lookupRaw(name) == InvalidValue) continue;  // bukan dari scope luar
            seen.insert(name);
            freeVars.push_back(name);
        }
    }

    const std::string name =
        "lambda." + std::to_string(syntheticCounter++);
    const FuncId func = builder.createFunction(name, InvalidType,
                                               expr->range.begin);
    functionsByName[name] = func;

    const FuncId previousFunc = currentFunc;
    const BlockId previousBlock = currentBlock;
    std::vector<ScopeEnv> previousEnv;
    previousEnv.swap(env);
    currentFunc = func;
    currentBlock = InvalidBlock;
    pushEnv();

    std::vector<TypeId> paramTypeIds;

    // Capture -> parameter implisit (paling awal), terikat ke nama asli.
    for (const std::string& capture : freeVars) {
        const ValueId raw = lookupRawIn(previousEnv, capture);
        TypeId captureType = InvalidType;
        if (raw != InvalidValue && previousFunc != InvalidFunc) {
            captureType =
                builder.function(previousFunc).values[raw].type;
        }
        const ValueId param = builder.addParam(func, captureType, capture);
        paramTypeIds.push_back(captureType);
        bindLocal(capture, param);
    }

    // Parameter deklarasi.
    for (const auto& [pname, ptype] : expr->parameters) {
        TypeId ptypeId = InvalidType;
        if (ptype) {
            ptypeId = mapType(
                semantic->context().getResolvedType(
                    const_cast<TypeReprNode*>(ptype)),
                expr);
            if (failed) return InvalidFunc;
        }
        const ValueId param = builder.addParam(func, ptypeId, pname);
        paramTypeIds.push_back(ptypeId);
        bindLocal(pname, param);
    }

    // Bentuk signature lambda: (capture..., param...) -> returnType.
    // Tanpa returnType eksplisit, pakai void untuk auto-terminator prosedur-style.
    IrType voidType;
    voidType.kind = IrTypeKind::Void;
    TypeId returnTypeId = builder.internType(voidType);
    if (expr->returnType) {
        returnTypeId = mapType(
            semantic->context().getResolvedType(
                const_cast<TypeReprNode*>(expr->returnType)),
            expr);
        if (failed) return InvalidFunc;
    }

    IrType fnType;
    fnType.kind = IrTypeKind::FnPtr;
    fnType.returnType = returnTypeId;
    fnType.paramTypes = std::move(paramTypeIds);
    builder.function(func).fnType = builder.internType(std::move(fnType));

    const BlockId entry = builder.createBlock(func, "entry",
                                              expr->body->range.begin);
    setCurrent(entry);
    if (!lowerStmt(expr->body)) {
        error(AstIrErrorCode::UnsupportedNode, expr->body,
              "lambda body lower gagal");
        return InvalidFunc;
    }

    IrFunction& fn = builder.function(func);
    if (blockOpen() &&
        (fn.blocks[currentBlock].insts.empty() ||
         !isTerminator(fn.instrs[fn.blocks[currentBlock].insts.back()].op))) {
        builder.setTerminator(func, currentBlock, IrOpcode::RetVoid, {},
                              expr->range.begin);
    }
    if (builder.broken()) return InvalidFunc;

    env.clear();
    previousEnv.swap(env);
    currentFunc = previousFunc;
    currentBlock = previousBlock;
    if (previousBlock != InvalidBlock && previousFunc != InvalidFunc) {
        builder.setInsertPoint(previousFunc, previousBlock);
    }
    return func;
}

} // namespace rhc
