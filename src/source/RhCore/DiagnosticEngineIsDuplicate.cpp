#include "RhCore/DiagnosticEngine.h"

namespace rhc {

bool DiagnosticEngine::isDuplicate(const Diagnostic& diag) const {
    for (const Diagnostic& existing : diagnostics_) {
        if (existing.severity == diag.severity &&
            existing.path == diag.path &&
            existing.line == diag.line &&
            existing.column == diag.column &&
            existing.length == diag.length &&
            existing.message == diag.message) {
            return true;
        }
    }
    return false;
}

} // namespace rhc
