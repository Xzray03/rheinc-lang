#pragma once

#include "RhCore/SourceLocation.h"
#include "RhCore/SourceManager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rhc {

enum class DiagnosticSeverity {
    Note,
    Warning,
    Error,
    Fatal,
};

struct Diagnostic {
    DiagnosticSeverity severity;
    std::string path;
    unsigned line = 1;
    unsigned column = 1;
    SourceLocation loc = 0;
    std::uint32_t length = 0;
    std::string message;
};

class DiagnosticEngine {
public:
    explicit DiagnosticEngine(const SourceManager& sourceManager,
                              std::size_t maxErrors = 20)
        : sourceManager_(sourceManager), maxErrors_(maxErrors) {}

    void report(DiagnosticSeverity severity, FileID file, SourceLocation loc,
                std::uint32_t length, std::string message);

    void note(FileID file, SourceLocation loc, std::uint32_t length, std::string message);
    void warning(FileID file, SourceLocation loc, std::uint32_t length, std::string message);
    void error(FileID file, SourceLocation loc, std::uint32_t length, std::string message);
    void fatal(FileID file, SourceLocation loc, std::uint32_t length, std::string message);

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }
    std::size_t errorCount() const { return errorCount_; }
    std::size_t warningCount() const { return warningCount_; }
    bool hasErrors() const { return errorCount_ != 0; }

private:
    const SourceManager& sourceManager_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t maxErrors_ = 20;
    std::size_t errorCount_ = 0;
    std::size_t warningCount_ = 0;
};

} // namespace rhc
