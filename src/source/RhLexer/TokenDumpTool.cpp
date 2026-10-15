#include "RhCore/DiagnosticEngine.h"
#include "RhCore/LangOptions.h"
#include "RhCore/SourceManager.h"
#include "RhLexer/IdentifierTable.h"
#include "RhLexer/Lexer.h"
#include "RhLexer/Token.h"
#include "RhLexer/TokenFlags.h"
#include "RhLexer/TokenKind.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

std::string escapeLexeme(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\'': out += "\\'"; break;
        default:
            if (c < 0x20u || c == 0x7Fu) {
                const char* hex = "0123456789abcdef";
                out += "\\x";
                out += hex[(c >> 4u) & 0xFu];
                out += hex[c & 0xFu];
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

const char* severityName(rhc::DiagnosticSeverity severity) {
    switch (severity) {
    case rhc::DiagnosticSeverity::Note: return "note";
    case rhc::DiagnosticSeverity::Warning: return "warning";
    case rhc::DiagnosticSeverity::Error: return "error";
    case rhc::DiagnosticSeverity::Fatal: return "fatal";
    }
    return "diagnostic";
}

void printFlags(std::ostream& out, rhc::TokenFlags flags) {
    bool first = true;
    auto emit = [&](const char* name) {
        if (!first) out << ',';
        out << name;
        first = false;
    };
    if (rhc::hasFlag(flags, rhc::TokenFlags::StartOfLine)) emit("start_of_line");
    if (rhc::hasFlag(flags, rhc::TokenFlags::LeadingWhitespace)) emit("leading_whitespace");
    if (rhc::hasFlag(flags, rhc::TokenFlags::RawMode)) emit("raw_mode");
    if (rhc::hasFlag(flags, rhc::TokenFlags::InDirective)) emit("in_directive");
    if (rhc::hasFlag(flags, rhc::TokenFlags::Invalid)) emit("invalid");
    if (rhc::hasFlag(flags, rhc::TokenFlags::EndOfDirective)) emit("end_of_directive");
    if (rhc::hasFlag(flags, rhc::TokenFlags::RawLiteral)) emit("raw_literal");
    if (first) out << "none";
}

} // namespace

int main(int argc, char** argv) {
    bool rawMode = false;
    const char* path = nullptr;
    if (argc == 2) {
        path = argv[1];
    } else if (argc == 3 && std::string_view(argv[1]) == "--raw") {
        rawMode = true;
        path = argv[2];
    } else {
        std::cerr << "usage: rhc-lex-dump [--raw] <file.rhc>\n";
        return 2;
    }

    rhc::SourceManager sourceManager;
    std::string error;
    const rhc::FileID file = sourceManager.addFileFromDisk(path, error);
    if (!file.valid()) {
        std::cerr << error << '\n';
        return 2;
    }

    rhc::IdentifierTable identifierTable;
    rhc::LangOptions options;
    rhc::DiagnosticEngine diagnostics(sourceManager, options.maxLexicalErrors);
    rhc::Lexer lexer(file, sourceManager, identifierTable, diagnostics, options);

    while (true) {
        const rhc::Token token = rawMode ? lexer.lexRaw() : lexer.lex();
        unsigned line = 1;
        unsigned column = 1;
        sourceManager.getLineAndColumn(file, token.loc, line, column);
        const std::string_view lexeme = sourceManager.getText(file, token.loc, token.length);

        std::cout << rhc::tokenKindName(token.kind)
                  << " '" << escapeLexeme(lexeme) << "' "
                  << line << ':' << column << " +" << token.length << " flags=";
        printFlags(std::cout, token.flags);
        if (token.ident) {
            std::cout << " ident=" << token.ident->getName();
        }
        std::cout << '\n';

        if (token.kind == rhc::TokenKind::Eof) {
            break;
        }
    }

    for (const rhc::Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.path << ':' << diagnostic.line << ':' << diagnostic.column
                  << ": " << severityName(diagnostic.severity) << ": "
                  << diagnostic.message << '\n';
    }

    return diagnostics.hasErrors() ? 1 : 0;
}
