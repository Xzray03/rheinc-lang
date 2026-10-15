#include "RhCore/DiagnosticEngine.h"

namespace rhc {

void DiagnosticEngine::report(DiagnosticSeverity severity, FileID file, SourceLocation loc,
                              std::uint32_t length, std::string message) {
    if ((severity == DiagnosticSeverity::Error || severity == DiagnosticSeverity::Fatal) &&
        errorCount_ >= maxErrors_) {
        return;
    }

    Diagnostic diag;
    diag.severity = severity;

    FileID spellingFile = file;
    FileOffset spellingOffset = sourceManager_.getFileOffset(loc);
    sourceManager_.getSpellingLocation(loc, spellingFile, spellingOffset);
    diag.path = sourceManager_.getPath(spellingFile);
    sourceManager_.getLineAndColumn(
        spellingFile,
        makeSourceLocation(spellingFile, spellingOffset),
        diag.line,
        diag.column);
    diag.loc = loc;
    diag.length = length;
    diag.message = std::move(message);

    if (severity == DiagnosticSeverity::Error || severity == DiagnosticSeverity::Fatal) {
        ++errorCount_;
    } else if (severity == DiagnosticSeverity::Warning) {
        ++warningCount_;
    }

    diagnostics_.push_back(std::move(diag));
}

void DiagnosticEngine::note(FileID file, SourceLocation loc, std::uint32_t length, std::string message) {
    report(DiagnosticSeverity::Note, file, loc, length, std::move(message));
}

void DiagnosticEngine::warning(FileID file, SourceLocation loc, std::uint32_t length, std::string message) {
    report(DiagnosticSeverity::Warning, file, loc, length, std::move(message));
}

void DiagnosticEngine::error(FileID file, SourceLocation loc, std::uint32_t length, std::string message) {
    report(DiagnosticSeverity::Error, file, loc, length, std::move(message));
}

void DiagnosticEngine::fatal(FileID file, SourceLocation loc, std::uint32_t length, std::string message) {
    report(DiagnosticSeverity::Fatal, file, loc, length, std::move(message));
}

} // namespace rhc
