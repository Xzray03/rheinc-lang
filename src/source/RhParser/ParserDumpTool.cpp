#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhLexer/TokenKind.h"
#include "RhParser/Decl.h"
#include "RhParser/Expr.h"
#include "RhParser/Parser.h"
#include "RhParser/Stmt.h"
#include "RhParser/TypeRepr.h"
#include "RhPreprocessor/Preprocessor.h"

#include <iostream>
#include <string>

namespace {

std::string quote(const std::string& input) {
    std::string out = "\"";
    for (char c : input) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        default: out.push_back(c); break;
        }
    }
    out += "\"";
    return out;
}

void printStmt(rhc::StmtNode* stmt);
void printPattern(rhc::PatternNode* pattern);
void printDecl(rhc::DeclNode* decl);
void printExpr(rhc::ExprNode* expr);

void printType(rhc::TypeReprNode* type) {
    if (!type) {
        std::cout << "(type <infer>)";
        return;
    }
    switch (type->kind) {
    case rhc::AstKind::BuiltinTypeRepr:
        std::cout << "(type " << rhc::tokenKindName(static_cast<rhc::BuiltinTypeRepr*>(type)->typeKind) << ")";
        break;
    case rhc::AstKind::NamedTypeRepr:
        std::cout << "(type " << static_cast<rhc::NamedTypeRepr*>(type)->name << ")";
        break;
    case rhc::AstKind::CallTypeRepr: {
        auto* call = static_cast<rhc::CallTypeRepr*>(type);
        std::cout << "(call-type " << call->name;
        for (rhc::TypeArgumentRepr* argument : call->arguments) {
            std::cout << ' ';
            if (!argument->name.empty()) {
                std::cout << "(named " << argument->name << ' ';
            }
            if (argument->type) {
                printType(argument->type);
            } else if (argument->value) {
                printExpr(argument->value);
            } else {
                std::cout << "(arg <error>)";
            }
            if (!argument->name.empty()) std::cout << ')';
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::ArrayTypeRepr: {
        auto* array = static_cast<rhc::ArrayTypeRepr*>(type);
        std::cout << "(array-type (";
        bool first = true;
        for (rhc::ExprNode* size : array->dimensions) {
            if (!first) std::cout << ' ';
            printExpr(size);
            first = false;
        }
        if (array->dimensions.empty()) std::cout << "<unsized>";
        std::cout << ") ";
        printType(array->elementType);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::PointerTypeRepr:
        std::cout << "(pointer-to ";
        printType(static_cast<rhc::PointerTypeRepr*>(type)->pointee);
        std::cout << ')';
        break;
    case rhc::AstKind::FunctionPointerTypeRepr: {
        auto* fn = static_cast<rhc::FunctionPointerTypeRepr*>(type);
        std::cout << "(function-pointer";
        if (fn->bare) {
            std::cout << " (bare)";
        } else {
            for (const auto& [name, paramType] : fn->parameters) {
                std::cout << " (param " << name << ' ';
                printType(paramType);
                std::cout << ')';
            }
            std::cout << " (return ";
            printType(fn->returnType);
            std::cout << ')';
        }
        std::cout << ')';
        break;
    }
    default:
        std::cout << "(type <error>)";
        break;
    }
}

void printExpr(rhc::ExprNode* expr) {
    if (!expr) {
        std::cout << "(expr <null>)";
        return;
    }
    switch (expr->kind) {
    case rhc::AstKind::IdentifierExpr:
        std::cout << "(identifier " << static_cast<rhc::IdentifierExpr*>(expr)->name << ")";
        break;
    case rhc::AstKind::IntegerLiteralExpr:
        std::cout << "(int " << static_cast<rhc::IntegerLiteralExpr*>(expr)->text << ")";
        break;
    case rhc::AstKind::CharLiteralExpr:
        std::cout << "(char " << quote(static_cast<rhc::CharLiteralExpr*>(expr)->text) << ")";
        break;
    case rhc::AstKind::BoolLiteralExpr:
        std::cout << '(' << (static_cast<rhc::BoolLiteralExpr*>(expr)->value ? "true" : "false") << ')';
        break;
    case rhc::AstKind::NullLiteralExpr:
        std::cout << "(null)";
        break;
    case rhc::AstKind::StringLiteralExpr:
        std::cout << "(string " << quote(static_cast<rhc::StringLiteralExpr*>(expr)->text) << ")";
        break;
    case rhc::AstKind::ArrayLiteralExpr: {
        std::cout << "(array-literal";
        for (rhc::ExprNode* element : static_cast<rhc::ArrayLiteralExpr*>(expr)->elements) {
            std::cout << ' ';
            printExpr(element);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::CallExpr: {
        auto* call = static_cast<rhc::CallExpr*>(expr);
        std::cout << "(call ";
        printExpr(call->callee);
        for (rhc::ExprNode* arg : call->arguments) {
            std::cout << ' ';
            printExpr(arg);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::BinaryExpr: {
        auto* binary = static_cast<rhc::BinaryExpr*>(expr);
        std::cout << '(' << rhc::tokenKindName(binary->op) << ' ';
        printExpr(binary->lhs);
        std::cout << ' ';
        printExpr(binary->rhs);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::UnaryExpr: {
        auto* unary = static_cast<rhc::UnaryExpr*>(expr);
        std::cout << '(' << (unary->postfix ? "post" : "pre") << '-'
                  << rhc::tokenKindName(unary->op) << ' ';
        printExpr(unary->operand);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::TernaryExpr: {
        auto* ternary = static_cast<rhc::TernaryExpr*>(expr);
        std::cout << "(ternary ";
        printExpr(ternary->condition);
        std::cout << ' ';
        printExpr(ternary->trueValue);
        std::cout << ' ';
        printExpr(ternary->falseValue);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::MemberExpr: {
        auto* member = static_cast<rhc::MemberExpr*>(expr);
        std::cout << "(member ";
        printExpr(member->object);
        std::cout << ' ' << member->memberName << ')';
        break;
    }
    case rhc::AstKind::LambdaExpr: {
        auto* lambda = static_cast<rhc::LambdaExpr*>(expr);
        std::cout << "(lambda";
        if (!lambda->typeParameters.empty()) {
            std::cout << " (generic";
            for (const std::string& param : lambda->typeParameters) std::cout << ' ' << param;
            std::cout << ')';
        }
        std::cout << " (params";
        for (const auto& [name, type] : lambda->parameters) {
            std::cout << " (param " << name << ' ';
            printType(type);
            std::cout << ')';
        }
        std::cout << ") (return ";
        printType(lambda->returnType);
        std::cout << ") ";
        printStmt(lambda->body);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::TypeExpr:
        std::cout << "(type-expr ";
        printType(static_cast<rhc::TypeExpr*>(expr)->type);
        std::cout << ')';
        break;
    case rhc::AstKind::NamedArgumentExpr: {
        auto* named = static_cast<rhc::NamedArgumentExpr*>(expr);
        std::cout << "(named-arg " << named->name << ' ';
        printExpr(named->value);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::SliceExpr: {
        auto* slice = static_cast<rhc::SliceExpr*>(expr);
        std::cout << "(slice";
        if (slice->start) { std::cout << ' '; printExpr(slice->start); }
        else std::cout << " <start>";
        if (slice->end) { std::cout << ' '; printExpr(slice->end); }
        else std::cout << " <end>";
        if (slice->step) { std::cout << ' '; printExpr(slice->step); }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::ArrayIndexExpr: {
        auto* index = static_cast<rhc::ArrayIndexExpr*>(expr);
        std::cout << "(index ";
        printExpr(index->object);
        std::cout << ' ';
        printExpr(index->index);
        std::cout << ')';
        break;
    }
    default:
        std::cout << "(expr <error>)";
        break;
    }
}


void printPattern(rhc::PatternNode* pattern) {
    if (!pattern) {
        std::cout << "(pattern <null>)";
        return;
    }
    switch (pattern->kind) {
    case rhc::AstKind::ExpressionPattern:
        std::cout << "(expr-pattern ";
        printExpr(static_cast<rhc::ExpressionPattern*>(pattern)->expression);
        std::cout << ')';
        break;
    case rhc::AstKind::ConstructorPattern: {
        auto* constructor = static_cast<rhc::ConstructorPattern*>(pattern);
        std::cout << "(constructor-pattern " << constructor->constructorName;
        for (rhc::PatternBinding* binding : constructor->bindings) {
            std::cout << " (binding " << binding->name;
            if (binding->type) {
                std::cout << ' ';
                printType(binding->type);
            }
            std::cout << ')';
        }
        std::cout << ')';
        break;
    }
    default:
        std::cout << "(pattern <error>)";
        break;
    }
}

void printStmt(rhc::StmtNode* stmt) {
    if (!stmt) return;
    switch (stmt->kind) {
    case rhc::AstKind::CompoundStmt: {
        std::cout << "(block";
        for (rhc::StmtNode* child : static_cast<rhc::CompoundStmt*>(stmt)->statements) {
            std::cout << ' ';
            printStmt(child);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::ReturnStmt: {
        std::cout << "(return";
        if (rhc::ExprNode* value = static_cast<rhc::ReturnStmt*>(stmt)->value) {
            std::cout << ' ';
            printExpr(value);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::ExprStmt:
        std::cout << "(expr-stmt ";
        printExpr(static_cast<rhc::ExprStmt*>(stmt)->expression);
        std::cout << ')';
        break;
    case rhc::AstKind::VarDeclStmt: {
        auto* var = static_cast<rhc::VarDeclStmt*>(stmt);
        std::cout << '(' << (var->isConst ? "const" : "var");
        if (var->storage == rhc::StorageModifier::TaskLocal) std::cout << "-task-local";
        if (var->storage == rhc::StorageModifier::TaskGlobal) std::cout << "-task-global";
        std::cout << ' ' << var->name << ' ';
        printType(var->type);
        if (var->initializer) {
            std::cout << ' ';
            printExpr(var->initializer);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::IfStmt: {
        auto* ifStmt = static_cast<rhc::IfStmt*>(stmt);
        std::cout << "(if ";
        printExpr(ifStmt->condition);
        std::cout << ' ';
        printStmt(ifStmt->thenBranch);
        if (ifStmt->elseBranch) {
            std::cout << ' ';
            printStmt(ifStmt->elseBranch);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::ForStmt: {
        auto* loop = static_cast<rhc::ForStmt*>(stmt);
        std::cout << '(' << (loop->isForEach ? "for-each" : "for");
        for (const std::string& var : loop->loopVariables) std::cout << ' ' << var;
        if (loop->loopVariables.empty()) std::cout << ' ' << loop->loopVariable;
        if (loop->isForEach) {
            std::cout << ' ';
            printExpr(loop->iterable);
        } else {
            std::cout << ' ';
            printExpr(loop->startValue);
            std::cout << ' ';
            printExpr(loop->endValue);
            if (loop->stepValue) {
                std::cout << ' ';
                printExpr(loop->stepValue);
            }
        }
        std::cout << ' ';
        printStmt(loop->body);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::DeclStmt:
        std::cout << "(decl-stmt ";
        printDecl(static_cast<rhc::DeclStmt*>(stmt)->declaration);
        std::cout << ')';
        break;
    case rhc::AstKind::GotoStmt:
        std::cout << "(goto " << static_cast<rhc::GotoStmt*>(stmt)->label << ")";
        break;
    case rhc::AstKind::LabelStmt: {
        auto* label = static_cast<rhc::LabelStmt*>(stmt);
        std::cout << "(label " << label->label << ' ';
        printStmt(label->statement);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::JaminStmt:
        std::cout << "(jamin ";
        printStmt(static_cast<rhc::JaminStmt*>(stmt)->body);
        std::cout << ')';
        break;
    case rhc::AstKind::WhileStmt: {
        auto* loop = static_cast<rhc::WhileStmt*>(stmt);
        std::cout << "(while ";
        printExpr(loop->condition);
        std::cout << ' ';
        printStmt(loop->body);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::DoWhileStmt: {
        auto* loop = static_cast<rhc::DoWhileStmt*>(stmt);
        std::cout << "(do-while ";
        printStmt(loop->body);
        std::cout << ' ';
        printExpr(loop->condition);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::JedaStmt: {
        std::cout << "(jeda";
        if (auto* cond = static_cast<rhc::JedaStmt*>(stmt)->untilCondition) {
            std::cout << ' ';
            printExpr(cond);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::KeluarStmt: {
        std::cout << "(keluar";
        if (auto* value = static_cast<rhc::KeluarStmt*>(stmt)->value) {
            std::cout << ' ';
            printExpr(value);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::BreakStmt:
        std::cout << "(break)";
        break;
    case rhc::AstKind::ContinueStmt:
        std::cout << "(continue)";
        break;
    case rhc::AstKind::PilihStmt: {
        auto* pilih = static_cast<rhc::PilihStmt*>(stmt);
        std::cout << "(pilih ";
        printExpr(pilih->subject);
        for (rhc::CaseClause* clause : pilih->clauses) {
            std::cout << ' ' << (clause->isDefault ? "(bawaan " : "(case");
            for (rhc::PatternNode* pattern : clause->patterns) {
                std::cout << ' ';
                printPattern(pattern);
            }
            std::cout << ' ';
            printStmt(clause->body);
            std::cout << ')';
        }
        std::cout << ')';
        break;
    }
    default:
        std::cout << "(stmt <error>)";
        break;
    }
}

void printDecl(rhc::DeclNode* decl) {
    if (!decl) return;
    switch (decl->kind) {
    case rhc::AstKind::FunctionDecl:
    case rhc::AstKind::ProcedureDecl: {
        auto* fn = static_cast<rhc::FunctionDecl*>(decl);
        std::cout << '(' << (fn->isProcedure ? "procedure" : "function");
        for (rhc::FunctionModifier modifier : fn->modifiers) {
            std::cout << ' ';
            switch (modifier) {
            case rhc::FunctionModifier::Async: std::cout << "async"; break;
            case rhc::FunctionModifier::Coroutine: std::cout << "coroutine"; break;
            case rhc::FunctionModifier::Generator: std::cout << "generator"; break;
            }
        }
        std::cout << ' ' << fn->name << " (params";
        for (rhc::ParamDecl* param : fn->parameters) {
            std::cout << " (param " << param->name << ' ';
            printType(param->type);
            std::cout << ')';
        }
        std::cout << ") (return ";
        printType(fn->returnType);
        std::cout << ')';
        if (fn->body) {
            std::cout << ' ';
            printStmt(fn->body);
        } else {
            std::cout << " (prototype)";
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::GlobalVarDecl: {
        auto* var = static_cast<rhc::GlobalVarDecl*>(decl);
        std::cout << '(' << (var->isConst ? "global-const" : "global-var");
        if (var->storage == rhc::StorageModifier::TaskGlobal) std::cout << "-task";
        std::cout << ' ' << var->name << ' ';
        printType(var->type);
        if (var->initializer) {
            std::cout << ' ';
            printExpr(var->initializer);
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::GenericDecl: {
        auto* generic = static_cast<rhc::GenericDecl*>(decl);
        std::cout << "(generic-decl (";
        bool first = true;
        for (const std::string& param : generic->typeParameters) {
            if (!first) std::cout << ' ';
            std::cout << param;
            first = false;
        }
        std::cout << ") ";
        printDecl(generic->declaration);
        std::cout << ')';
        break;
    }
    case rhc::AstKind::EnumDecl: {
        auto* enumDecl = static_cast<rhc::EnumDecl*>(decl);
        std::cout << "(enum " << enumDecl->name;
        for (rhc::EnumField* field : enumDecl->fields) {
            std::cout << " (case " << field->name;
            for (const auto& [name, type] : field->fields) {
                std::cout << " (field " << name << ' ';
                printType(type);
                std::cout << ')';
            }
            if (field->value) {
                std::cout << " (value ";
                printExpr(field->value);
                std::cout << ')';
            }
            std::cout << ')';
        }
        std::cout << ')';
        break;
    }
    case rhc::AstKind::TestAnnotationDecl: {
        auto* annotation = static_cast<rhc::TestAnnotationDecl*>(decl);
        std::cout << "(test " << annotation->name << ')';
        break;
    }
    case rhc::AstKind::StructDecl: {
        auto* struktur = static_cast<rhc::StructDecl*>(decl);
        std::cout << "(struct " << struktur->name;
        for (rhc::StructField* field : struktur->fields) {
            std::cout << " (field " << field->name << ' ';
            printType(field->type);
            if (field->bitWidth) {
                std::cout << " : ";
                printExpr(field->bitWidth);
            }
            std::cout << ')';
        }
        std::cout << ')';
        break;
    }
    default:
        std::cout << "(decl <error>)";
        break;
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: rheinc-parse-dump <file.rhc>\n";
        return 2;
    }

    rhc::SourceManager sourceManager;
    std::string error;
    const rhc::FileID file = sourceManager.addFileFromDisk(argv[1], error);
    if (!file.valid()) {
        std::cerr << error << '\n';
        return 2;
    }

    rhc::IdentifierTable identifiers;
    rhc::LangOptions options;
    rhc::DiagnosticEngine diagnostics(sourceManager, options.maxLexicalErrors);
    rhc::Preprocessor preprocessor(sourceManager, diagnostics, identifiers, options);
    preprocessor.setMainFile(file);
    rhc::Parser parser(preprocessor, sourceManager, diagnostics);
    rhc::TranslationUnitDecl* unit = parser.parseTranslationUnit();

    std::cout << "(translation-unit";
    for (rhc::TestAnnotationDecl* annotation : unit->testAnnotations) {
        std::cout << "\n  (test " << annotation->name;
        for (rhc::ExprNode* argument : annotation->arguments) {
            std::cout << ' ';
            printExpr(argument);
        }
        std::cout << ')';
    }
    for (rhc::DeclNode* decl : unit->declarations) {
        std::cout << "\n  ";
        printDecl(decl);
    }
    std::cout << ")\n";

    for (const rhc::Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.path << ':' << diagnostic.line << ':' << diagnostic.column
                  << ": error: " << diagnostic.message << '\n';
    }
    return diagnostics.hasErrors() ? 1 : 0;
}
