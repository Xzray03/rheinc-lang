#include "RhCore/DiagnosticEngine.h"

#include <csignal>
#include <cstdlib>
#include <utility>

namespace rhc {

[[noreturn]] void DiagnosticEngine::fatal(FileID file, SourceLocation loc, std::uint32_t length, std::string message) {
    report(DiagnosticSeverity::Fatal, file, loc, length, std::move(message));
    // report(Fatal) sudah terminasi; fallback agar [[noreturn]] terpenuhi.
    std::abort();
}

} // namespace rhc
