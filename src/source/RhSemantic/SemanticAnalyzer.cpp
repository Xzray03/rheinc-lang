#include "RhSemantic/SemanticAnalyzer.h"
#include "RhSemantic/Type.h"

#include <string>
#include <utility>

namespace rhc {

bool SemanticAnalyzer::analyze(TranslationUnitDecl* unit) {
    if (!unit) return false;
    currentScope_ = &context_.globalScope();

    for (DeclNode* decl : unit->declarations) {
        collectDeclaration(decl);
    }
    for (DeclNode* decl : unit->declarations) {
        analyzeDeclaration(decl);
    }

    currentScope_ = &context_.globalScope();
    return !diagnostics_.hasErrors();
}

void SemanticAnalyzer::error(SourceRange range, const std::string& message) {
    diagnostics_.error(sourceManager_.getFileID(range.begin), range.begin, range.length, message);
}

void SemanticAnalyzer::insertGlobalAllowingBuiltinShadow(Symbol* symbol) {
    Symbol* existing = context_.globalScope().lookupLocal(symbol->name);
    if (existing && isBuiltinSymbol(*existing)) {
        context_.globalScope().insertOrReplace(symbol);
        return;
    }
    if (!context_.globalScope().insert(symbol)) {
        error(symbol->range, "redefinition of name: " + symbol->name);
    }
}

bool SemanticAnalyzer::isDefinitelyAssigned(Symbol* symbol) const {
    auto it = assigned_.find(symbol);
    if (it == assigned_.end()) return true; // params, globals, captures: not tracked
    return it->second;
}

bool SemanticAnalyzer::statementTerminates(StmtNode* stmt) const {
    if (!stmt) return false;
    switch (stmt->kind) {
        case AstKind::ReturnStmt:
        case AstKind::KeluarStmt:
            return true;
        case AstKind::CompoundStmt: {
            auto* compound = static_cast<CompoundStmt*>(stmt);
            for (StmtNode* s : compound->statements) {
                if (statementTerminates(s)) return true;
            }
            return false;
        }
        case AstKind::IfStmt: {
            auto* jika = static_cast<IfStmt*>(stmt);
            if (!jika->elseBranch) return false; // may fall through when condition false
            return statementTerminates(jika->thenBranch) &&
                   statementTerminates(jika->elseBranch);
        }
        case AstKind::PilihStmt: {
            auto* pilih = static_cast<PilihStmt*>(stmt);
            if (pilih->clauses.empty()) return false;
            for (CaseClause* clause : pilih->clauses) {
                if (!statementTerminates(clause->body)) return false;
            }
            return true;
        }
        case AstKind::DoWhileStmt:
            // ulangi { } selama cond: the body always executes at least once.
            return statementTerminates(static_cast<DoWhileStmt*>(stmt)->body);
        default:
            return false;
    }
}

void SemanticAnalyzer::checkReturnPaths(StmtNode* body, Type* returnType, SourceRange range) {
    if (!returnType || returnType == context_.types().getBuiltinType(BuiltinTypeKind::Hampa)) {
        return;
    }
    if (!statementTerminates(body)) {
        error(range, "not all code paths return a value (kembalikan)");
    }
}

SemanticAnalyzer::SemanticAnalyzer(SourceManager& sourceManager,
                                   DiagnosticEngine& diagnostics)
    : sourceManager_(sourceManager), diagnostics_(diagnostics),
      resolver_(context_, diagnostics_) {
    builtins_.registerAll(context_);
}

} // namespace rhc
