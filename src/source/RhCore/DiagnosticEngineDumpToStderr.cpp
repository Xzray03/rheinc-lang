#include "RhCore/DiagnosticEngine.h"

#include <csignal>
#include <cstdio>

namespace rhc {

void DiagnosticEngine::dumpToStderr() const {
    static const char* kSeverity[] = {"note", "warning", "error", "fatal"};
    // stderr bisa berupa pipe yang sudah ditutup pembacanya (mis. rheinc
    // | head). SIGPIPE default akan membunuh proses SEBELUM terminate-
    // ProcessTree sempat membunuh proses turunan -> abaikan sementara.
    const auto oldPipe = std::signal(SIGPIPE, SIG_IGN);
    for (const Diagnostic& d : diagnostics_) {
        const int sev = static_cast<int>(d.severity);
        std::fprintf(stderr, "%s:%u:%u: %s: %s\n",
                     d.path.c_str(), d.line, d.column,
                     kSeverity[sev < 0 || sev > 3 ? 2 : sev],
                     d.message.c_str());
    }
    std::fflush(stderr);
    std::signal(SIGPIPE, oldPipe);
}

} // namespace rhc
