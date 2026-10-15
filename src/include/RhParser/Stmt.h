#pragma once

#include "RhParser/AstNode.h"
#include "RhParser/Expr.h"
#include "RhParser/Pattern.h"
#include "RhParser/TypeRepr.h"

namespace rhc { struct DeclNode; }

#include <vector>

namespace rhc {

struct StmtNode : AstNode {
    using AstNode::AstNode;
};

struct CompoundStmt final : StmtNode {
    std::vector<StmtNode*> statements;

    explicit CompoundStmt(SourceRange range) : StmtNode(AstKind::CompoundStmt, range) {}
};

struct DeclStmt final : StmtNode {
    DeclNode* declaration = nullptr;

    explicit DeclStmt(DeclNode* decl, SourceRange range)
        : StmtNode(AstKind::DeclStmt, range), declaration(decl) {}
};

struct VarDeclStmt final : StmtNode {
    std::string name;
    TypeReprNode* type = nullptr;
    ExprNode* initializer = nullptr;
    bool isConst = false;
    StorageModifier storage = StorageModifier::None;

    VarDeclStmt(std::string variableName, TypeReprNode* variableType,
                ExprNode* init, bool constant, SourceRange range,
                StorageModifier storageModifier = StorageModifier::None)
        : StmtNode(AstKind::VarDeclStmt, range), name(std::move(variableName)),
          type(variableType), initializer(init), isConst(constant), storage(storageModifier) {}
};

struct IfStmt final : StmtNode {
    ExprNode* condition = nullptr;
    StmtNode* thenBranch = nullptr;
    StmtNode* elseBranch = nullptr;

    IfStmt(ExprNode* cond, StmtNode* thenStmt, StmtNode* elseStmt, SourceRange range)
        : StmtNode(AstKind::IfStmt, range), condition(cond), thenBranch(thenStmt),
          elseBranch(elseStmt) {}
};

struct ForStmt final : StmtNode {
    std::string loopVariable;
    std::vector<std::string> loopVariables;
    ExprNode* startValue = nullptr;
    ExprNode* endValue = nullptr;
    ExprNode* stepValue = nullptr;
    ExprNode* iterable = nullptr;
    StmtNode* body = nullptr;
    bool isForEach = false;

    ForStmt(std::string variable, std::vector<std::string> variables,
            ExprNode* start, ExprNode* end, ExprNode* step,
            ExprNode* collection, StmtNode* loopBody, bool each, SourceRange range)
        : StmtNode(AstKind::ForStmt, range), loopVariable(std::move(variable)),
          loopVariables(std::move(variables)),
          startValue(start), endValue(end), stepValue(step), iterable(collection),
          body(loopBody), isForEach(each) {}
};

struct WhileStmt final : StmtNode {
    ExprNode* condition = nullptr;
    StmtNode* body = nullptr;

    WhileStmt(ExprNode* cond, StmtNode* loopBody, SourceRange range)
        : StmtNode(AstKind::WhileStmt, range), condition(cond), body(loopBody) {}
};

struct DoWhileStmt final : StmtNode {
    StmtNode* body = nullptr;
    ExprNode* condition = nullptr;

    DoWhileStmt(StmtNode* loopBody, ExprNode* cond, SourceRange range)
        : StmtNode(AstKind::DoWhileStmt, range), body(loopBody), condition(cond) {}
};

struct BreakStmt final : StmtNode {
    explicit BreakStmt(SourceRange range) : StmtNode(AstKind::BreakStmt, range) {}
};

struct ContinueStmt final : StmtNode {
    explicit ContinueStmt(SourceRange range) : StmtNode(AstKind::ContinueStmt, range) {}
};

struct CaseClause final : StmtNode {
    bool isDefault = false;
    std::vector<PatternNode*> patterns;
    StmtNode* body = nullptr;

    CaseClause(bool defaultCase, std::vector<PatternNode*> casePatterns,
               StmtNode* clauseBody, SourceRange range)
        : StmtNode(AstKind::CaseClause, range), isDefault(defaultCase),
          patterns(std::move(casePatterns)), body(clauseBody) {}
};

struct PilihStmt final : StmtNode {
    ExprNode* subject = nullptr;
    std::vector<CaseClause*> clauses;

    PilihStmt(ExprNode* value, std::vector<CaseClause*> caseClauses, SourceRange range)
        : StmtNode(AstKind::PilihStmt, range), subject(value), clauses(std::move(caseClauses)) {}
};

struct GotoStmt final : StmtNode {
    std::string label;

    explicit GotoStmt(std::string target, SourceRange range)
        : StmtNode(AstKind::GotoStmt, range), label(std::move(target)) {}
};

struct LabelStmt final : StmtNode {
    std::string label;
    StmtNode* statement = nullptr;

    LabelStmt(std::string name, StmtNode* body, SourceRange range)
        : StmtNode(AstKind::LabelStmt, range), label(std::move(name)), statement(body) {}
};

struct JaminStmt final : StmtNode {
    StmtNode* body = nullptr;

    explicit JaminStmt(StmtNode* guarded, SourceRange range)
        : StmtNode(AstKind::JaminStmt, range), body(guarded) {}
};

struct JedaStmt final : StmtNode {
    ExprNode* untilCondition = nullptr;

    explicit JedaStmt(ExprNode* condition, SourceRange range)
        : StmtNode(AstKind::JedaStmt, range), untilCondition(condition) {}
};

struct KeluarStmt final : StmtNode {
    ExprNode* value = nullptr;

    explicit KeluarStmt(ExprNode* exitValue, SourceRange range)
        : StmtNode(AstKind::KeluarStmt, range), value(exitValue) {}
};

struct ReturnStmt final : StmtNode {
    ExprNode* value = nullptr;

    ReturnStmt(ExprNode* returnValue, SourceRange range)
        : StmtNode(AstKind::ReturnStmt, range), value(returnValue) {}
};

struct ExprStmt final : StmtNode {
    ExprNode* expression = nullptr;

    explicit ExprStmt(ExprNode* expr, SourceRange range)
        : StmtNode(AstKind::ExprStmt, range), expression(expr) {}
};

struct ErrorStmt final : StmtNode {
    ErrorStmt(SourceRange range) : StmtNode(AstKind::ErrorStmt, range) {}
};

} // namespace rhc
