#include "AstToIrTranslatorImpl.h"

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

// Pre-pass address-taken per fungsi: variabel yang operand dari
// unary "alamat"/& (langsung, bukan member) dipaksa memory-style.
void AstToIrTranslator::Impl::collectAddressTaken(const StmtNode* body) {
    addressTaken.clear();
    collectStmt(body, addressTaken);
}

} // namespace rhc
