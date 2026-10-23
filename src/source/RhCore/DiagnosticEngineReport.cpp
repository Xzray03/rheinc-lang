#include "RhCore/DiagnosticEngine.h"

#include <utility>

namespace rhc {

void DiagnosticEngine::report(DiagnosticSeverity severity, FileID file, SourceLocation loc,
                              std::uint32_t length, std::string message) {
    const bool isError = severity == DiagnosticSeverity::Error;
    const bool isFatal = severity == DiagnosticSeverity::Fatal;

    // Note yatim ditolak: note hanya sah menyusul diagnostic yang benar-benar
    // tercatat (rantai konteks error/warning tidak terputus oleh cap/dedup).
    if (severity == DiagnosticSeverity::Note && !noteChainOpen_) {
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

    // Dedup: diagnostic identik (severity + lokasi + pesan) tidak dicatat
    // ulang — mencegah banjir duplikat dari fase yang sama.
    if (isDuplicate(diag)) {
        return;
    }

    // Batas jumlah error. Fatal tidak tunduk pada batas ini: kondisi
    // tak-terpulihkan harus selalu tercatat sebelum terminasi.
    if (isError && errorCount_ >= maxErrors_) {
        if (!capNoticeShown_) {
            capNoticeShown_ = true;
            noteChainOpen_ = false;
            Diagnostic notice;
            notice.severity = DiagnosticSeverity::Note;
            notice.path = diag.path;
            notice.line = diag.line;
            notice.column = diag.column;
            notice.message = "error tambahan dihilangkan (batas " +
                             std::to_string(maxErrors_) + " error tercapai)";
            diagnostics_.push_back(std::move(notice));
        }
        return;
    }

    if (isError || isFatal) {
        ++errorCount_;
    } else if (severity == DiagnosticSeverity::Warning) {
        ++warningCount_;
    }
    noteChainOpen_ = true;
    diagnostics_.push_back(std::move(diag));

    if (isFatal) {
        dumpToStderr();
        terminateProcessTree();
    }
}

} // namespace rhc
