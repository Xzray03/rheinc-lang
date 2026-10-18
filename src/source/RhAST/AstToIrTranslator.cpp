#include "RhAST/AstToIrTranslator.h"

#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhAST/IrDump.h"
#include "RhAST/ValidateAst.h"
#include "RhSemantic/SemanticAnalyzer.h"
#include "RhParser/Decl.h"
#include "RhSemantic/Type.h"

#include <iostream>
#include <sstream>
#include <utility>

namespace rhc {

// ===== Impl: konstruktor & utilitas diagnostic =====

AstToIrTranslator::Impl::Impl(SourceManager& sourceManagerRef,
                              DiagnosticEngine& diagnosticsRef,
                              AstToIrOptions optionsValue)
    : sourceManager(sourceManagerRef), diagnostics(diagnosticsRef),
      options(optionsValue),
      builder(module, diagnosticsRef,
              IrBuilderOptions{optionsValue.verifyAfterEachMutation}) {}

bool AstToIrTranslator::Impl::error(AstIrErrorCode code, const AstNode* node,
                                    const std::string& detail) {
    std::string message = astIrErrorCodeString(code);
    message += ": ";
    message += detail;
    const SourceLocation loc = node ? node->range.begin : 0;
    const std::uint32_t length = node ? node->range.length : 0;
    diagnostics.error(FileID(FileID::invalidValue), loc, length, message);
    failed = true;
    return false;
}

bool AstToIrTranslator::Impl::fatal(const std::string& detail) {
    diagnostics.fatal(FileID(FileID::invalidValue), 0, 0, "IrAST: " + detail);
    failed = true;
    return false;
}

const Type* AstToIrTranslator::Impl::exprTypeOr(const ExprNode* expr,
                                                const TypeReprNode* repr) {
    const Type* type = semantic->context().getExprType(
        const_cast<ExprNode*>(expr));
    if (type && type->kind() == TypeKind::Error && repr) {
        const Type* resolved = semantic->context().getResolvedType(
            const_cast<TypeReprNode*>(repr));
        if (resolved && resolved->kind() != TypeKind::Error) return resolved;
    }
    return type;
}

ValueId AstToIrTranslator::Impl::emitCallByName(
        const std::string& name, std::vector<ValueId> args,
        SourceLocation loc) {
    FuncId fn = InvalidFunc;
    const auto found = functionsByName.find(name);
    if (found != functionsByName.end()) {
        fn = found->second;
    } else {
        fn = builder.createFunction(name, InvalidType, loc, IrFunctionKind::External);
        // Stub builtin/runtime: di-link ke runtime bahasa, bukan ke
        // definisi user (lihat IrLinkVerifier).
        builder.function(fn).isBuiltin = true;
        functionsByName[name] = fn;
        module.globals[name] = fn;
    }
    TypeId resultType = InvalidType;
    if (const Symbol* symbol = lookupGlobalSymbol(name)) {
        if (symbol->type && symbol->type->kind() == TypeKind::Function) {
            const auto* fnType = static_cast<const FunctionType*>(symbol->type);
            if (fnType->returnType && !typeContainsError(fnType->returnType)) {
                resultType = mapType(fnType->returnType, nullptr);
            }
        }
    }
    if (resultType == InvalidType) {
        IrType voidType;
        voidType.kind = IrTypeKind::Void;
        resultType = builder.internType(voidType);
    }
    std::vector<IrOperand> operands;
    operands.push_back(IrOperand::func(fn));
    for (ValueId arg : args) operands.push_back(IrOperand::value(arg));
    return builder.emit(IrOpcode::Call, resultType, std::move(operands), loc);
}

const Symbol* AstToIrTranslator::Impl::lookupGlobalSymbol(
        const std::string& name) const {
    if (!semantic) return nullptr;
    return semantic->context().globalScope().lookup(name);
}

bool AstToIrTranslator::Impl::isStructTypeName(const std::string& name) const {
    return lookupStructType(name) != nullptr;
}

const Type* AstToIrTranslator::Impl::lookupStructType(
        const std::string& name) const {
    if (!semantic) return nullptr;
    for (const Symbol* symbol : semantic->context().module().structs) {
        if (symbol->name == name) return symbol->type;
    }
    return nullptr;
}

// true bila tipe mengandung ErrorType di KEDALAMAN (bukan hanya
// permukaan). RhSemantic kadang memberi ErrorType tanpa diagnostic
// (mis. panggilan fungsi tak dikenal yang dibiarkan) — tipe hasil
// seperti PointerType{ErrorType} tidak boleh meledakkan mapType di
// titik yang tidak relevan; pemetaannya ditunda seperti ErrorType
// permukaan, dan error jujur hanya muncul bila nilai benar-benar
// butuh tipe.
bool typeContainsError(const Type* type) {
    if (!type) return true;   // tipe absen dianggap tidak valid
    if (type->kind() == TypeKind::Error) return true;
    if (type->kind() == TypeKind::Pointer) {
        return typeContainsError(
            static_cast<const PointerType*>(type)->pointee);
    }
    if (type->kind() == TypeKind::Array) {
        return typeContainsError(
            static_cast<const ArrayType*>(type)->elementType);
    }
    if (type->kind() == TypeKind::Slice) {
        return typeContainsError(
            static_cast<const SliceType*>(type)->elementType);
    }
    if (type->kind() == TypeKind::Function) {
        const auto* fn = static_cast<const FunctionType*>(type);
        if (fn->returnType && typeContainsError(fn->returnType)) return true;
        for (const FunctionParamType& param : fn->params) {
            if (typeContainsError(param.type)) return true;
        }
        return false;
    }
    return false;
}

ValueId AstToIrTranslator::Impl::lookupRaw(const std::string& name) const {
    for (auto it = env.rbegin(); it != env.rend(); ++it) {
        const auto found = it->vars.find(name);
        if (found != it->vars.end()) return found->second;
    }
    return InvalidValue;
}

ValueId AstToIrTranslator::Impl::lookupLocal(const std::string& name) {
    const ValueId stored = lookupRaw(name);
    if (stored == InvalidValue) {
        // Global mutable: baca lewat Load dari storage global.
        const auto global = module.globalVarIndex.find(name);
        if (global != module.globalVarIndex.end()) {
            return builder.emit(IrOpcode::Load,
                                module.globalVars[global->second].type,
                                {IrOperand::global(global->second)}, 0, name);
        }
        return InvalidValue;
    }
    if (!isMemoryStyle(name)) return stored;
    // Variabel memory-style: baca lewat Load dari ptr Alloc.
    const IrFunction& fn = builder.function(currentFunc);
    const TypeId ptrType = fn.values[stored].type;
    const TypeId elemType = module.types[ptrType].elementType;
    return builder.emit(IrOpcode::Load, elemType, {IrOperand::value(stored)},
                        0, name);
}

void AstToIrTranslator::Impl::bindLocal(const std::string& name,
                                        ValueId value) {
    env.back().vars[name] = value;
    if (currentFunc != InvalidFunc) {
        builder.function(currentFunc).locals[name] = value;
    }
}

void AstToIrTranslator::Impl::assignLocal(const std::string& name,
                                          ValueId value) {
    // Assignment SELALU menulis scope terdalam (shadow) agar merge
    // if/else (yang membandingkan snapshot env.back()) melihat perubahan.
    // Scope body loop ditangkap via capturePoppedEnv saat pop, jadi
    // nilai loop-carried tetap terbaca oleh wiring phi.
    env.back().vars[name] = value;
    if (currentFunc != InvalidFunc) {
        builder.function(currentFunc).locals[name] = value;
    }
}

// ===== AstToIrTranslator: shell publik =====

AstToIrTranslator::AstToIrTranslator(SourceManager& sourceManager,
                                     DiagnosticEngine& diagnostics,
                                     AstToIrOptions options)
    : impl_(std::make_unique<Impl>(sourceManager, diagnostics, options)) {}

AstToIrTranslator::~AstToIrTranslator() = default;

IrModule* AstToIrTranslator::translate(TranslationUnitDecl* unit,
                                       SemanticAnalyzer& semantic) {
    if (impl_->failed || impl_->moduleReleased) return nullptr;

    // Prasyarat keras (spesifikasi Bagian 1.2): RhAST tidak pernah
    // berjalan jika SemanticAnalyzer menghasilkan satu pun Error/Fatal.
    if (impl_->diagnostics.hasErrors()) {
        impl_->diagnostics.fatal(FileID(FileID::invalidValue), 0, 0,
            "RhAST: dilarang berjalan — diagnostic sebelumnya sudah error");
        impl_->failed = true;
        return nullptr;
    }

    // Fase 1 — validasi AST (read-only, hard-stop error pertama).
    if (!ValidateAst(*unit, impl_->diagnostics)) {
        impl_->failed = true;
        return nullptr;
    }

    // Fase 2 — lowering.
    impl_->semantic = &semantic;
    impl_->unit = unit;
    if (!impl_->lowerTranslationUnit() || impl_->builder.broken()) {
        impl_->failed = true;
        return nullptr;
    }
    if (impl_->options.verifyAfterEachMutation) {
        std::string verifyError;
        if (!impl_->builder.verifyModule(&verifyError)) {
            return nullptr;  // Fatal sudah masuk DiagnosticEngine
        }
    }
    return &impl_->module;
}

IrModule& AstToIrTranslator::module() { return impl_->module; }
const IrModule& AstToIrTranslator::module() const { return impl_->module; }

std::unique_ptr<IrModule> AstToIrTranslator::releaseModule() {
    if (!impl_ || impl_->moduleReleased) {
        return nullptr;
    }
    impl_->moduleReleased = true;
    return std::make_unique<IrModule>(std::move(impl_->module));
}

const TypeMapping& AstToIrTranslator::typeMapping() const {
    return impl_->module.typeMapping;
}

const SymbolMapping& AstToIrTranslator::symbolMapping() const {
    return impl_->module.symbolMapping;
}

void AstToIrTranslator::dump(std::ostream& out, const IrDumpOptions& options) const {
    dumpIrModule(out, impl_->module, options);
}

} // namespace rhc
