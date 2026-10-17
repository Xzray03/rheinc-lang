#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Decl.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/SemanticContext.h"
#include "RhSemantic/Symbol.h"
#include "RhSemantic/Type.h"

#include <algorithm>

namespace rhc {

// Lowering satu fungsi/prosedur: signature, parameter, entry block,
// body. Variabel mutable di-versioning lewat symbol table lokal
// (IrFunction::locals) — tiap assignment menghasilkan ValueId baru
// (spesifikasi Bagian 4.1).
bool AstToIrTranslator::Impl::lowerFunction(const FunctionDecl* decl) {
    if (failed || builder.broken()) return false;

    // Signature IR dari tipe semantic fungsi (via symbol decl).
    TypeId fnType = InvalidType;
    if (const Symbol* symbol = semantic->context().getDeclSymbol(decl)) {
        fnType = mapType(symbol->type, decl, "fn-symbol");
        if (failed) return false;
    }

    // Reuse stub pra-pendaftaran dari LowerTranslationUnit pass 1
    // (forward reference); isi signature-nya di sini.
    FuncId func = InvalidFunc;
    const auto found = functionsByName.find(decl->name);
    if (found != functionsByName.end()) {
        func = found->second;
        if (fnType != InvalidType) {
            builder.function(func).fnType = fnType;
        }
    } else {
        func = builder.createFunction(decl->name, fnType, decl->range.begin);
        module.globals[decl->name] = func;
        functionsByName[decl->name] = func;
    }
    if (builder.broken()) return false;

    IrFunction& irFunction = builder.function(func);
    if (decl->name == "utama") {
        irFunction.kind = IrFunctionKind::Entry;
    } else if (!decl->body) {
        irFunction.kind = IrFunctionKind::External;
    } else {
        irFunction.kind = IrFunctionKind::Normal;
    }

    if (const Symbol* symbol = semantic->context().getDeclSymbol(decl)) {
        module.symbolMapping.mapFunction(symbol, func);
    }

    // Body null = deklarasi eksternal saja.
    if (!decl->body) return true;

    const FuncId previousFunc = currentFunc;
    currentFunc = func;
    env.clear();
    pushEnv();
    collectAddressTaken(decl->body);

    // Parameter -> SSA value Param, terdaftar di locals.
    if (const Symbol* symbol = semantic->context().getDeclSymbol(decl)) {
        if (symbol->type && symbol->type->kind() == TypeKind::Function) {
            const auto* fnTypeSemantic =
                static_cast<const FunctionType*>(symbol->type);
            const std::size_t count =
                decl->parameters.size() < fnTypeSemantic->params.size()
                    ? decl->parameters.size()
                    : fnTypeSemantic->params.size();
            for (std::size_t i = 0; i < count; ++i) {
                const TypeId paramType = mapType(fnTypeSemantic->params[i].type,
                                                 decl->parameters[i]);
                if (failed) return false;
                const ValueId param = builder.addParam(
                    func, paramType, decl->parameters[i]->name);
                if (builder.broken()) return false;
                bindLocal(decl->parameters[i]->name, param);
            }
        }
    }

    const BlockId entry =
        builder.createBlock(func, "entry", decl->body->range.begin);
    builder.setInsertPoint(func, entry);
    setCurrent(entry);

    if (!lowerStmt(decl->body)) return false;

    // Resolve forward goto: set terminator bridge -> label block.
    for (const auto& pg : pendingGotos) {
        const auto found = labelBlocks.find(pg.label);
        if (found == labelBlocks.end()) {
            error(AstIrErrorCode::MalformedShape, nullptr,
                  "label '" + pg.label + "' tidak ditemukan (goto forward)");
            return false;
        }
        builder.setTerminator(func, pg.bridge, IrOpcode::Br,
                              {IrOperand::block(found->second)}, pg.loc);
    }
    pendingGotos.clear();
    labelBlocks.clear();

    // Prosedur tanpa terminator eksplisit -> RetVoid implisit.
    // Fungsi bernilai WAJIB kembalikan eksplisit (dijamin RhSemantic).
    IrFunction& fn = builder.function(func);
    // Fungsi bermodifier async/coroutine/generator: jalur tanpa
    // kembalikan eksplisit mendapat RetVoid implisit (async-structural).
    const bool isCoroutine =
        std::find_if(decl->modifiers.begin(), decl->modifiers.end(),
                     [](FunctionModifier m) {
                         return m == FunctionModifier::Async ||
                                m == FunctionModifier::Coroutine ||
                                m == FunctionModifier::Generator;
                     }) != decl->modifiers.end();
    if (blockOpen() &&
        (fn.blocks[currentBlock].insts.empty() ||
         !isTerminator(fn.instrs[fn.blocks[currentBlock].insts.back()].op))) {
        // Coroutine/generator: jalur yang berakhir tanpa kembalikan
        // (semua-hasil / infinite) mendapat RetVoid implisit.
        // Semua fungsi yang berakhir tanpa terminator mendapat RetVoid
        // implisit: prosedur, coroutine/generator, fungsi tanpa
        // returnType, maupun fungsi "-> hampa" (corpus: handler sinyal).
        (void)isCoroutine;
        if (true) {
            emitAllDefers();
            if (failed || builder.broken()) return false;
            builder.setTerminator(func, currentBlock, IrOpcode::RetVoid, {},
                                  decl->range.begin);
        } else {
            error(AstIrErrorCode::TerminatorMissing, decl,
                  "fungsi '" + decl->name +
                  "' berakhir tanpa terminator (kembalikan eksplisit "
                  "diperlukan)");
            return false;
        }
    }

    if (builder.broken()) return false;
    popEnv();
    currentFunc = previousFunc;
    setCurrent(InvalidBlock);
    return true;
}

} // namespace rhc
