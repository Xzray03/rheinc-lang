#include "RhAST/AstToIrTranslatorImpl.h"

#include "RhParser/Decl.h"
#include "RhParser/Expr.h"
#include "RhParser/Stmt.h"
#include "RhParser/TypeRepr.h"

namespace rhc {

namespace {

void collectExpr(const ExprNode* expr,
                 std::unordered_set<std::string>& out) {
    if (!expr) return;
    switch (expr->kind) {
    case AstKind::UnaryExpr: {
        const auto* node = static_cast<const UnaryExpr*>(expr);
        if (node->op == UnaryOp::AddressOf &&
            node->operand->kind == AstKind::IdentifierExpr) {
            out.insert(static_cast<const IdentifierExpr*>(node->operand)->name);
        }
        collectExpr(node->operand, out);
        break;
    }
    case AstKind::BinaryExpr: {
        const auto* node = static_cast<const BinaryExpr*>(expr);
        collectExpr(node->lhs, out);
        collectExpr(node->rhs, out);
        break;
    }
    case AstKind::CallExpr: {
        const auto* node = static_cast<const CallExpr*>(expr);
        collectExpr(node->callee, out);
        for (const ExprNode* arg : node->arguments) collectExpr(arg, out);
        break;
    }
    case AstKind::NamedArgumentExpr: {
        const auto* node = static_cast<const NamedArgumentExpr*>(expr);
        collectExpr(node->value, out);
        break;
    }
    case AstKind::TernaryExpr: {
        const auto* node = static_cast<const TernaryExpr*>(expr);
        collectExpr(node->condition, out);
        collectExpr(node->trueValue, out);
        collectExpr(node->falseValue, out);
        break;
    }
    case AstKind::MemberExpr: {
        const auto* node = static_cast<const MemberExpr*>(expr);
        collectExpr(node->object, out);
        break;
    }
    case AstKind::ArrayIndexExpr: {
        const auto* node = static_cast<const ArrayIndexExpr*>(expr);
        collectExpr(node->object, out);
        collectExpr(node->index, out);
        break;
    }
    case AstKind::SliceExpr: {
        const auto* node = static_cast<const SliceExpr*>(expr);
        collectExpr(node->start, out);
        collectExpr(node->end, out);
        collectExpr(node->step, out);
        break;
    }
    case AstKind::ArrayLiteralExpr: {
        const auto* node = static_cast<const ArrayLiteralExpr*>(expr);
        for (const ExprNode* element : node->elements) {
            collectExpr(element, out);
        }
        break;
    }
    default:
        break;
    }
}

void collectStmt(const StmtNode* stmt,
                 std::unordered_set<std::string>& out) {
    if (!stmt) return;
    switch (stmt->kind) {
    case AstKind::CompoundStmt: {
        const auto* node = static_cast<const CompoundStmt*>(stmt);
        for (const StmtNode* child : node->statements) {
            collectStmt(child, out);
        }
        break;
    }
    case AstKind::ExprStmt:
        collectExpr(static_cast<const ExprStmt*>(stmt)->expression, out);
        break;
    case AstKind::ReturnStmt:
        collectExpr(static_cast<const ReturnStmt*>(stmt)->value, out);
        break;
    case AstKind::VarDeclStmt: {
        const auto* node = static_cast<const VarDeclStmt*>(stmt);
        collectExpr(node->initializer, out);
        break;
    }
    case AstKind::IfStmt: {
        const auto* node = static_cast<const IfStmt*>(stmt);
        collectExpr(node->condition, out);
        collectStmt(node->thenBranch, out);
        collectStmt(node->elseBranch, out);
        break;
    }
    case AstKind::WhileStmt: {
        const auto* node = static_cast<const WhileStmt*>(stmt);
        collectExpr(node->condition, out);
        collectStmt(node->body, out);
        break;
    }
    case AstKind::DoWhileStmt: {
        const auto* node = static_cast<const DoWhileStmt*>(stmt);
        collectStmt(node->body, out);
        collectExpr(node->condition, out);
        break;
    }
    case AstKind::ForStmt: {
        const auto* node = static_cast<const ForStmt*>(stmt);
        collectExpr(node->startValue, out);
        collectExpr(node->endValue, out);
        collectExpr(node->stepValue, out);
        collectStmt(node->body, out);
        break;
    }
    case AstKind::LabelStmt: {
        const auto* node = static_cast<const LabelStmt*>(stmt);
        collectStmt(node->statement, out);
        break;
    }
    case AstKind::JaminStmt: {
        const auto* node = static_cast<const JaminStmt*>(stmt);
        collectStmt(node->body, out);
        break;
    }
    default:
        break;
    }
}

} // namespace

// Kumpulkan SEMUA identifier yang dipakai dalam ekspresi/statement
// (eksternal — dipakai LowerLambda.cpp untuk deteksi capture).
void collectStmtIdentifiers(const StmtNode* stmt,
                            std::vector<std::string>& out);

void collectIdentifiers(const ExprNode* expr,
                        std::vector<std::string>& out) {
    if (!expr) return;
    switch (expr->kind) {
    case AstKind::IdentifierExpr:
        out.push_back(static_cast<const IdentifierExpr*>(expr)->name);
        break;
    case AstKind::BinaryExpr: {
        const auto* n = static_cast<const BinaryExpr*>(expr);
        collectIdentifiers(n->lhs, out);
        collectIdentifiers(n->rhs, out);
        break;
    }
    case AstKind::UnaryExpr:
        collectIdentifiers(static_cast<const UnaryExpr*>(expr)->operand, out);
        break;
    case AstKind::CallExpr: {
        const auto* n = static_cast<const CallExpr*>(expr);
        collectIdentifiers(n->callee, out);
        for (const ExprNode* a : n->arguments) collectIdentifiers(a, out);
        break;
    }
    case AstKind::NamedArgumentExpr:
        collectIdentifiers(
            static_cast<const NamedArgumentExpr*>(expr)->value, out);
        break;
    case AstKind::TernaryExpr: {
        const auto* n = static_cast<const TernaryExpr*>(expr);
        collectIdentifiers(n->condition, out);
        collectIdentifiers(n->trueValue, out);
        collectIdentifiers(n->falseValue, out);
        break;
    }
    case AstKind::MemberExpr:
        collectIdentifiers(static_cast<const MemberExpr*>(expr)->object, out);
        break;
    case AstKind::ArrayIndexExpr: {
        const auto* n = static_cast<const ArrayIndexExpr*>(expr);
        collectIdentifiers(n->object, out);
        collectIdentifiers(n->index, out);
        break;
    }
    case AstKind::SliceExpr: {
        const auto* n = static_cast<const SliceExpr*>(expr);
        collectIdentifiers(n->start, out);
        collectIdentifiers(n->end, out);
        collectIdentifiers(n->step, out);
        break;
    }
    case AstKind::ArrayLiteralExpr: {
        const auto* n = static_cast<const ArrayLiteralExpr*>(expr);
        for (const ExprNode* e : n->elements) collectIdentifiers(e, out);
        break;
    }
    case AstKind::LambdaExpr: {
        const auto* n = static_cast<const LambdaExpr*>(expr);
        for (const StmtNode* st : n->body->statements) {
            collectStmtIdentifiers(st, out);
        }
        break;
    }
    default:
        break;
    }
}

void collectStmtIdentifiers(const StmtNode* stmt,
                            std::vector<std::string>& out) {
    if (!stmt) return;
    switch (stmt->kind) {
    case AstKind::CompoundStmt: {
        const auto* n = static_cast<const CompoundStmt*>(stmt);
        for (const StmtNode* c : n->statements) collectStmtIdentifiers(c, out);
        break;
    }
    case AstKind::ExprStmt:
        collectIdentifiers(static_cast<const ExprStmt*>(stmt)->expression, out);
        break;
    case AstKind::ReturnStmt:
        collectIdentifiers(static_cast<const ReturnStmt*>(stmt)->value, out);
        break;
    case AstKind::VarDeclStmt: {
        const auto* n = static_cast<const VarDeclStmt*>(stmt);
        collectIdentifiers(n->initializer, out);
        break;
    }
    case AstKind::IfStmt: {
        const auto* n = static_cast<const IfStmt*>(stmt);
        collectIdentifiers(n->condition, out);
        collectStmtIdentifiers(n->thenBranch, out);
        collectStmtIdentifiers(n->elseBranch, out);
        break;
    }
    case AstKind::WhileStmt: {
        const auto* n = static_cast<const WhileStmt*>(stmt);
        collectIdentifiers(n->condition, out);
        collectStmtIdentifiers(n->body, out);
        break;
    }
    case AstKind::DoWhileStmt: {
        const auto* n = static_cast<const DoWhileStmt*>(stmt);
        collectStmtIdentifiers(n->body, out);
        collectIdentifiers(n->condition, out);
        break;
    }
    case AstKind::ForStmt: {
        const auto* n = static_cast<const ForStmt*>(stmt);
        collectIdentifiers(n->startValue, out);
        collectIdentifiers(n->endValue, out);
        collectIdentifiers(n->stepValue, out);
        collectIdentifiers(n->iterable, out);
        collectStmtIdentifiers(n->body, out);
        break;
    }
    case AstKind::PilihStmt: {
        const auto* n = static_cast<const PilihStmt*>(stmt);
        collectIdentifiers(n->subject, out);
        for (const CaseClause* c : n->clauses) {
            collectStmtIdentifiers(c->body, out);
        }
        break;
    }
    case AstKind::JaminStmt:
        collectStmtIdentifiers(static_cast<const JaminStmt*>(stmt)->body, out);
        break;
    case AstKind::LabelStmt:
        collectStmtIdentifiers(
            static_cast<const LabelStmt*>(stmt)->statement, out);
        break;
    default:
        break;
    }
}

// Pre-pass address-taken per fungsi: variabel yang operand dari
// unary "alamat"/& (langsung, bukan member) dipaksa memory-style.
void AstToIrTranslator::Impl::collectAddressTaken(const StmtNode* body) {
    addressTaken.clear();
    collectStmt(body, addressTaken);
}

} // namespace rhc
