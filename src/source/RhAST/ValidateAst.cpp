#include "RhAST/ValidateAst.h"

#include "RhAST/IrErrorCatalog.h"
#include "RhParser/Decl.h"
#include "RhParser/Expr.h"
#include "RhParser/Pattern.h"
#include "RhParser/Stmt.h"
#include "RhParser/TypeRepr.h"

#include <string>

namespace rhc {

namespace {

// Walker validasi read-only. Setiap kegagalan = hard-stop: report()
// mengembalikan false dan caller langsung propagate tanpa melanjutkan.
class AstValidator {
public:
    explicit AstValidator(DiagnosticEngine& diagnostics)
        : diagnostics_(diagnostics) {}

    bool validate(const TranslationUnitDecl& unit) {
        for (const DeclNode* decl : unit.declarations) {
            if (!checkDecl(decl, "deklarasi top-level")) return false;
        }
        for (const TestAnnotationDecl* annotation : unit.testAnnotations) {
            (void)annotation;
            return fail(nullptr, AstIrErrorCode::UnsupportedNode,
                        "anotasi uji belum didukung RhAST");
        }
        return true;
    }

private:
    DiagnosticEngine& diagnostics_;

    bool fail(const AstNode* node, AstIrErrorCode code,
              const std::string& detail) {
        std::string message = astIrErrorCodeString(code);
        message += ": ";
        message += detail;
        const SourceLocation loc = node ? node->range.begin : 0;
        const std::uint32_t length = node ? node->range.length : 0;
        diagnostics_.error(FileID(FileID::invalidValue), loc, length, message);
        return false;
    }

    bool require(const AstNode* child, const AstNode* parent,
                 const char* what) {
        if (child) return true;
        std::string detail = "child null pada posisi wajib: ";
        detail += what;
        return fail(parent, AstIrErrorCode::MalformedShape, detail);
    }

    bool requireName(const std::string& name, const AstNode* parent,
                     const char* what) {
        if (!name.empty()) return true;
        std::string detail = "nama kosong pada: ";
        detail += what;
        return fail(parent, AstIrErrorCode::MalformedShape, detail);
    }

    // ===== Decl =====

    bool checkDecl(const DeclNode* decl, const char* role) {
        if (!require(decl, nullptr, role)) return false;
        switch (decl->kind) {
        case AstKind::ErrorDecl:
            return fail(decl, AstIrErrorCode::ErrorNode,
                        "ErrorDecl (hasil recovery parser) lolos ke RhAST");
        case AstKind::GenericDecl: {
            const auto* node = static_cast<const GenericDecl*>(decl);
            if (node->typeParameters.empty()) {
                return fail(decl, AstIrErrorCode::MalformedShape,
                            "GenericDecl tanpa type parameter");
            }
            if (!require(node->declaration, decl,
                         "GenericDecl.declaration")) {
                return false;
            }
            // Isi: fungsi/struktur/cacahan generik — divalidasi sebagai
            // deklarasi biasa (parameter generik = TypeVariable).
            return checkDecl(node->declaration, "isi GenericDecl");
        }
        case AstKind::EnumDecl: {
            const auto* node = static_cast<const EnumDecl*>(decl);
            if (!requireName(node->name, decl, "EnumDecl.name")) return false;
            for (const EnumField* field : node->fields) {
                if (!checkDecl(field, "kasus enum")) return false;
            }
            return true;
        }
        case AstKind::EnumField: {
            const auto* node = static_cast<const EnumField*>(decl);
            if (!requireName(node->name, decl, "EnumField.name")) return false;
            if (node->value && !checkExpr(node->value, "nilai kasus enum")) {
                return false;
            }
            return true;
        }
        case AstKind::TestAnnotationDecl:
            return fail(decl, AstIrErrorCode::UnsupportedNode,
                        "anotasi uji belum didukung RhAST");
        case AstKind::ParamDecl: {
            const auto* node = static_cast<const ParamDecl*>(decl);
            if (!requireName(node->name, decl, "ParamDecl.name")) return false;
            return checkTypeRepr(node->type, decl, "ParamDecl.type");
        }
        case AstKind::FunctionDecl:
        case AstKind::ProcedureDecl: {
            const auto* node = static_cast<const FunctionDecl*>(decl);
            if (!requireName(node->name, decl, "FunctionDecl.name")) return false;
            if (!node->modifiers.empty()) {
                return fail(decl, AstIrErrorCode::UnsupportedNode,
                            "fungsi ber-modifier (async/coroutine/generator) "
                            "belum didukung RhAST");
            }
            // Trait constraints ("dimana T: Terbandingkan") diabaikan
            // saat lowering — pengecekan trait adalah tugas RhSemantic;
            // RhAST menerima bentuk yang sudah lolos semantic.
            for (const ParamDecl* param : node->parameters) {
                if (!checkDecl(param, "parameter fungsi")) return false;
            }
            if (!node->isProcedure && !node->returnType) {
                return fail(decl, AstIrErrorCode::MalformedShape,
                            "fungsi non-prosedur tanpa returnType");
            }
            if (node->returnType &&
                !checkTypeRepr(node->returnType, decl,
                               "FunctionDecl.returnType")) {
                return false;
            }
            if (node->body) {
                if (node->body->kind != AstKind::CompoundStmt) {
                    return fail(decl, AstIrErrorCode::MalformedShape,
                                "body fungsi bukan CompoundStmt");
                }
                if (!checkStmt(node->body, "body fungsi")) return false;
            }
            return true;
        }
        case AstKind::StructDecl: {
            const auto* node = static_cast<const StructDecl*>(decl);
            if (!requireName(node->name, decl, "StructDecl.name")) return false;
            for (const StructField* field : node->fields) {
                if (!checkDecl(field, "field struct")) return false;
            }
            return true;
        }
        case AstKind::StructField: {
            const auto* node = static_cast<const StructField*>(decl);
            if (!requireName(node->name, decl, "StructField.name")) return false;
            if (!checkTypeRepr(node->type, decl, "StructField.type")) {
                return false;
            }
            if (node->bitWidth && !checkExpr(node->bitWidth, "lebar bit field")) {
                return false;
            }
            return true;
        }
        case AstKind::GlobalVarDecl: {
            const auto* node = static_cast<const GlobalVarDecl*>(decl);
            if (!requireName(node->name, decl, "GlobalVarDecl.name")) {
                return false;
            }
            if (!node->type && !node->initializer) {
                return fail(decl, AstIrErrorCode::MalformedShape,
                            "global tanpa type maupun initializer");
            }
            if (node->type &&
                !checkTypeRepr(node->type, decl, "GlobalVarDecl.type")) {
                return false;
            }
            if (node->initializer &&
                !checkExpr(node->initializer, "initializer global")) {
                return false;
            }
            return true;
        }
        default:
            return fail(decl, AstIrErrorCode::UnsupportedNode,
                        "jenis deklarasi belum didukung RhAST");
        }
    }

    // ===== Stmt =====

    bool checkStmt(const StmtNode* stmt, const char* role) {
        if (!require(stmt, nullptr, role)) return false;
        switch (stmt->kind) {
        case AstKind::ErrorStmt:
            return fail(stmt, AstIrErrorCode::ErrorNode,
                        "ErrorStmt (hasil recovery parser) lolos ke RhAST");
        case AstKind::PilihStmt: {
            const auto* node = static_cast<const PilihStmt*>(stmt);
            if (!require(node->subject, stmt, "PilihStmt.subject")) return false;
            if (!checkExpr(node->subject, "subjek pilih")) return false;
            for (const CaseClause* clause : node->clauses) {
                if (!checkStmt(clause, "kasus pilih")) return false;
            }
            return true;
        }
        case AstKind::CaseClause: {
            const auto* node = static_cast<const CaseClause*>(stmt);
            if (node->patterns.empty() && !node->isDefault) {
                return fail(stmt, AstIrErrorCode::MalformedShape,
                            "kasus tanpa pattern dan bukan bawaan");
            }
            for (const PatternNode* pattern : node->patterns) {
                if (!checkPattern(pattern)) return false;
            }
            // Body boleh null (fall-through ke kasus berikutnya).
            if (node->body && !checkStmt(node->body, "body kasus")) {
                return false;
            }
            return true;
        }
        case AstKind::JedaStmt:
            return fail(stmt, AstIrErrorCode::UnsupportedNode,
                        "jeda (generator) belum didukung RhAST");
        case AstKind::KeluarStmt:
            return fail(stmt, AstIrErrorCode::UnsupportedNode,
                        "keluar proses belum didukung RhAST");
        case AstKind::CompoundStmt: {
            const auto* node = static_cast<const CompoundStmt*>(stmt);
            for (const StmtNode* child : node->statements) {
                if (!checkStmt(child, "statement di dalam blok")) return false;
            }
            return true;
        }
        case AstKind::DeclStmt: {
            const auto* node = static_cast<const DeclStmt*>(stmt);
            if (!require(node->declaration, stmt,
                         "DeclStmt.declaration")) {
                return false;
            }
            return checkDecl(node->declaration, "deklarasi di statement");
        }
        case AstKind::VarDeclStmt: {
            const auto* node = static_cast<const VarDeclStmt*>(stmt);
            if (!requireName(node->name, stmt, "VarDeclStmt.name")) return false;
            if (!node->type && !node->initializer) {
                return fail(stmt, AstIrErrorCode::MalformedShape,
                            "var tanpa type maupun initializer");
            }
            if (node->type &&
                !checkTypeRepr(node->type, stmt, "VarDeclStmt.type")) {
                return false;
            }
            if (node->initializer &&
                !checkExpr(node->initializer, "initializer var")) {
                return false;
            }
            return true;
        }
        case AstKind::IfStmt: {
            const auto* node = static_cast<const IfStmt*>(stmt);
            if (!require(node->condition, stmt, "IfStmt.condition")) return false;
            if (!require(node->thenBranch, stmt, "IfStmt.thenBranch")) {
                return false;
            }
            if (!checkExpr(node->condition, "kondisi jika")) return false;
            if (!checkStmt(node->thenBranch, "cabang maka")) return false;
            if (node->elseBranch &&
                !checkStmt(node->elseBranch, "cabang selain-itu")) {
                return false;
            }
            return true;
        }
        case AstKind::ForStmt: {
            const auto* node = static_cast<const ForStmt*>(stmt);
            if (node->isForEach) {
                // foreach: iterable + 1-2 loop variable (elemen, atau
                // indeks + elemen). Body wajib.
                if (node->loopVariable.empty() && node->loopVariables.empty()) {
                    return fail(stmt, AstIrErrorCode::MalformedShape,
                                "foreach tanpa loop variable");
                }
                if (!require(node->iterable, stmt,
                             "ForStmt.iterable (foreach)")) {
                    return false;
                }
                if (!checkExpr(node->iterable, "iterable foreach")) {
                    return false;
                }
                return checkStmt(node->body, "body foreach");
            }
            if (node->loopVariable.empty() && node->loopVariables.empty()) {
                return fail(stmt, AstIrErrorCode::MalformedShape,
                            "ForStmt tanpa loop variable");
            }
            if (!require(node->startValue, stmt, "ForStmt.startValue")) {
                return false;
            }
            if (!require(node->endValue, stmt, "ForStmt.endValue")) return false;
            if (!require(node->body, stmt, "ForStmt.body")) return false;
            if (!checkExpr(node->startValue, "nilai awal perulangan")) {
                return false;
            }
            if (!checkExpr(node->endValue, "nilai akhir perulangan")) {
                return false;
            }
            if (node->stepValue &&
                !checkExpr(node->stepValue, "langkah perulangan")) {
                return false;
            }
            return checkStmt(node->body, "body perulangan");
        }
        case AstKind::WhileStmt: {
            const auto* node = static_cast<const WhileStmt*>(stmt);
            if (!require(node->condition, stmt, "WhileStmt.condition")) {
                return false;
            }
            if (!require(node->body, stmt, "WhileStmt.body")) return false;
            if (!checkExpr(node->condition, "kondisi selama")) return false;
            return checkStmt(node->body, "body selama");
        }
        case AstKind::DoWhileStmt: {
            const auto* node = static_cast<const DoWhileStmt*>(stmt);
            if (!require(node->body, stmt, "DoWhileStmt.body")) return false;
            if (!require(node->condition, stmt, "DoWhileStmt.condition")) {
                return false;
            }
            if (!checkStmt(node->body, "body ulangi-selama")) return false;
            return checkExpr(node->condition, "kondisi ulangi-selama");
        }
        case AstKind::GotoStmt: {
            const auto* node = static_cast<const GotoStmt*>(stmt);
            return requireName(node->label, stmt, "GotoStmt.label");
        }
        case AstKind::LabelStmt: {
            const auto* node = static_cast<const LabelStmt*>(stmt);
            if (!requireName(node->label, stmt, "LabelStmt.label")) return false;
            if (!require(node->statement, stmt, "LabelStmt.statement")) {
                return false;
            }
            return checkStmt(node->statement, "statement berlabel");
        }
        case AstKind::JaminStmt: {
            const auto* node = static_cast<const JaminStmt*>(stmt);
            if (!require(node->body, stmt, "JaminStmt.body")) return false;
            if (node->body->kind != AstKind::CompoundStmt) {
                return fail(stmt, AstIrErrorCode::MalformedShape,
                            "body jamin bukan blok");
            }
            return checkStmt(node->body, "body jamin");
        }
        case AstKind::ReturnStmt: {
            const auto* node = static_cast<const ReturnStmt*>(stmt);
            if (node->value && !checkExpr(node->value, "nilai kembalikan")) {
                return false;
            }
            return true;
        }
        case AstKind::ExprStmt: {
            const auto* node = static_cast<const ExprStmt*>(stmt);
            if (!require(node->expression, stmt, "ExprStmt.expression")) {
                return false;
            }
            return checkExpr(node->expression, "ekspresi statement");
        }
        case AstKind::BreakStmt:
        case AstKind::ContinueStmt:
            return true;
        default:
            return fail(stmt, AstIrErrorCode::UnsupportedNode,
                        "jenis statement belum didukung RhAST");
        }
    }

    // ===== Expr =====

    bool checkExpr(const ExprNode* expr, const char* role) {
        if (!require(expr, nullptr, role)) return false;
        switch (expr->kind) {
        case AstKind::ErrorExpr:
            return fail(expr, AstIrErrorCode::ErrorNode,
                        "ErrorExpr (hasil recovery parser) lolos ke RhAST");
        case AstKind::LambdaExpr:
            return fail(expr, AstIrErrorCode::UnsupportedNode,
                        "lambda belum didukung RhAST");
        case AstKind::IdentifierExpr: {
            const auto* node = static_cast<const IdentifierExpr*>(expr);
            return requireName(node->name, expr, "IdentifierExpr.name");
        }
        case AstKind::IntegerLiteralExpr:
        case AstKind::CharLiteralExpr: {
            const auto* node =
                static_cast<const IntegerLiteralExpr*>(expr);
            (void)node;  // teks literal boleh berupa token apa pun
            return true;
        }
        case AstKind::StringLiteralExpr: {
            return true;  // string kosong ("") legal
        }
        case AstKind::BoolLiteralExpr:
        case AstKind::NullLiteralExpr:
            return true;
        case AstKind::ArrayLiteralExpr: {
            const auto* node = static_cast<const ArrayLiteralExpr*>(expr);
            for (const ExprNode* element : node->elements) {
                if (!checkExpr(element, "elemen array literal")) return false;
            }
            return true;
        }
        case AstKind::CallExpr: {
            const auto* node = static_cast<const CallExpr*>(expr);
            if (!require(node->callee, expr, "CallExpr.callee")) return false;
            if (!checkExpr(node->callee, "callee panggilan")) return false;
            for (const ExprNode* arg : node->arguments) {
                if (!checkExpr(arg, "argumen panggilan")) return false;
            }
            return true;
        }
        case AstKind::NamedArgumentExpr: {
            const auto* node = static_cast<const NamedArgumentExpr*>(expr);
            if (!requireName(node->name, expr, "NamedArgumentExpr.name")) {
                return false;
            }
            if (!require(node->value, expr, "NamedArgumentExpr.value")) {
                return false;
            }
            return checkExpr(node->value, "nilai argumen bernama");
        }
        case AstKind::BinaryExpr: {
            const auto* node = static_cast<const BinaryExpr*>(expr);
            if (!require(node->lhs, expr, "BinaryExpr.lhs")) return false;
            if (!require(node->rhs, expr, "BinaryExpr.rhs")) return false;
            if (node->op == TokenKind::Unknown) {
                return fail(expr, AstIrErrorCode::MalformedShape,
                            "BinaryExpr dengan op Unknown");
            }
            if (!checkExpr(node->lhs, "operand kiri")) return false;
            return checkExpr(node->rhs, "operand kanan");
        }
        case AstKind::UnaryExpr: {
            const auto* node = static_cast<const UnaryExpr*>(expr);
            if (node->op == UnaryOp::Await) {
                return fail(expr, AstIrErrorCode::UnsupportedNode,
                            "await belum didukung RhAST");
            }
            if (!require(node->operand, expr, "UnaryExpr.operand")) return false;
            return checkExpr(node->operand, "operand unary");
        }
        case AstKind::TernaryExpr: {
            const auto* node = static_cast<const TernaryExpr*>(expr);
            if (!require(node->condition, expr, "TernaryExpr.condition")) {
                return false;
            }
            if (!require(node->trueValue, expr, "TernaryExpr.trueValue")) {
                return false;
            }
            if (!require(node->falseValue, expr, "TernaryExpr.falseValue")) {
                return false;
            }
            if (!checkExpr(node->condition, "kondisi ternary")) return false;
            if (!checkExpr(node->trueValue, "cabang ternary")) return false;
            return checkExpr(node->falseValue, "cabang ternary");
        }
        case AstKind::MemberExpr: {
            const auto* node = static_cast<const MemberExpr*>(expr);
            if (!require(node->object, expr, "MemberExpr.object")) return false;
            if (!requireName(node->memberName, expr, "MemberExpr.memberName")) {
                return false;
            }
            return checkExpr(node->object, "objek akses member");
        }
        case AstKind::ArrayIndexExpr: {
            const auto* node = static_cast<const ArrayIndexExpr*>(expr);
            if (!require(node->object, expr, "ArrayIndexExpr.object")) {
                return false;
            }
            if (!require(node->index, expr, "ArrayIndexExpr.index")) return false;
            if (!checkExpr(node->object, "objek indeks")) return false;
            return checkExpr(node->index, "indeks");
        }
        case AstKind::SliceExpr: {
            // SliceExpr hanya membawa batas (start/end/step); objek yang
            // di-slice menjadi context dari ekspresi indeks di sekitarnya.
            const auto* node = static_cast<const SliceExpr*>(expr);
            if (node->start && !checkExpr(node->start, "awal slice")) return false;
            if (node->end && !checkExpr(node->end, "akhir slice")) return false;
            if (node->step && !checkExpr(node->step, "langkah slice")) return false;
            return true;
        }
        case AstKind::TypeExpr: {
            const auto* node = static_cast<const TypeExpr*>(expr);
            if (!require(node->type, expr, "TypeExpr.type")) return false;
            return checkTypeRepr(node->type, expr, "TypeExpr.type");
        }
        default:
            return fail(expr, AstIrErrorCode::UnsupportedNode,
                        "jenis ekspresi belum didukung RhAST");
        }
    }

    // ===== Pattern =====

    bool checkPattern(const PatternNode* pattern) {
        if (!require(pattern, nullptr, "pattern")) return false;
        switch (pattern->kind) {
        case AstKind::ExpressionPattern: {
            const auto* node = static_cast<const ExpressionPattern*>(pattern);
            if (!require(node->expression, pattern,
                         "ExpressionPattern.expression")) {
                return false;
            }
            return checkExpr(node->expression, "pattern ekspresi");
        }
        case AstKind::ConstructorPattern: {
            const auto* node = static_cast<const ConstructorPattern*>(pattern);
            if (!requireName(node->constructorName, pattern,
                             "ConstructorPattern.constructorName")) {
                return false;
            }
            for (const PatternBinding* binding : node->bindings) {
                if (!checkBinding(binding)) return false;
            }
            return true;
        }
        default:
            return fail(pattern, AstIrErrorCode::UnsupportedNode,
                        "jenis pattern belum didukung RhAST");
        }
    }

    bool checkBinding(const PatternBinding* binding) {
        if (!require(binding, nullptr, "PatternBinding")) return false;
        if (!requireName(binding->name, binding, "PatternBinding.name")) {
            return false;
        }
        if (binding->type &&
            !checkTypeRepr(binding->type, binding, "PatternBinding.type")) {
            return false;
        }
        if (binding->nestedPattern &&
            !checkPattern(binding->nestedPattern)) {
            return false;
        }
        return true;
    }

    // ===== TypeRepr =====

    bool checkTypeRepr(const TypeReprNode* type, const AstNode* parent,
                       const char* role) {
        if (!require(type, parent, role)) return false;
        switch (type->kind) {
        case AstKind::ErrorTypeRepr:
            return fail(type, AstIrErrorCode::ErrorNode,
                        "ErrorTypeRepr (hasil recovery parser) lolos ke RhAST");
        case AstKind::BuiltinTypeRepr:
        case AstKind::NamedTypeRepr:
            return true;
        case AstKind::CallTypeRepr: {
            const auto* node = static_cast<const CallTypeRepr*>(type);
            if (!requireName(node->name, type, "CallTypeRepr.name")) {
                return false;
            }
            for (const TypeArgumentRepr* arg : node->arguments) {
                if (!require(arg, type, "TypeArgumentRepr")) return false;
                // Tiga bentuk legal: nama parameter generik (deklarasi),
                // tipe konkret (pemakaian), atau NILAI argumen
                // (mis. bilangan_tanpa_batas("1"), desimal_tetap(x, skala=2)).
                if (arg->name.empty() && !arg->type && !arg->value) {
                    return fail(type, AstIrErrorCode::MalformedShape,
                                "TypeArgumentRepr tanpa nama, tipe, maupun "
                                "nilai");
                }
                if (arg->type &&
                    !checkTypeRepr(arg->type, type,
                                   "argumen tipe generik")) {
                    return false;
                }
                if (arg->value &&
                    !checkExpr(arg->value, "nilai argumen tipe")) {
                    return false;
                }
            }
            return true;
        }
        case AstKind::ArrayTypeRepr: {
            const auto* node = static_cast<const ArrayTypeRepr*>(type);
            if (!require(node->elementType, type,
                         "ArrayTypeRepr.elementType")) {
                return false;
            }
            if (!checkTypeRepr(node->elementType, type, "elemen array")) {
                return false;
            }
            for (const ExprNode* dim : node->dimensions) {
                if (dim && !checkExpr(dim, "dimensi array")) return false;
            }
            return true;
        }
        case AstKind::PointerTypeRepr: {
            const auto* node = static_cast<const PointerTypeRepr*>(type);
            if (!require(node->pointee, type, "PointerTypeRepr.pointee")) {
                return false;
            }
            return checkTypeRepr(node->pointee, type, "pointee");
        }
        case AstKind::FunctionPointerTypeRepr: {
            const auto* node =
                static_cast<const FunctionPointerTypeRepr*>(type);
            if (!node->bare && !node->returnType) {
                return fail(type, AstIrErrorCode::MalformedShape,
                            "tipe pointer fungsi tanpa returnType");
            }
            for (const auto& [paramName, paramType] : node->parameters) {
                (void)paramName;
                if (!checkTypeRepr(paramType, type,
                                   "parameter tipe fungsi")) {
                    return false;
                }
            }
            if (node->returnType &&
                !checkTypeRepr(node->returnType, type, "return type")) {
                return false;
            }
            return true;
        }
        default:
            return fail(type, AstIrErrorCode::UnsupportedNode,
                        "jenis representasi tipe belum didukung RhAST");
        }
    }
};

} // namespace

bool ValidateAst(const TranslationUnitDecl& unit,
                 DiagnosticEngine& diagnostics) {
    AstValidator validator(diagnostics);
    return validator.validate(unit);
}

} // namespace rhc
