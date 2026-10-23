#include "RhCore/DiagnosticEngine.h"

#include <utility>

namespace rhc {

void DiagnosticEngine::warning(FileID file, SourceLocation loc, std::uint32_t length, std::string message) {
    report(DiagnosticSeverity::Warning, file, loc, length, std::move(message));
}

} // namespace rhc
