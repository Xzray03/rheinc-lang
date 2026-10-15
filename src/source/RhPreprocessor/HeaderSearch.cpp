#include "RhPreprocessor/HeaderSearch.h"

#include <filesystem>

namespace rhc {
namespace fs = std::filesystem;

void HeaderSearch::addIncludeDirectory(std::string path) {
    includeDirs_.push_back(std::move(path));
}

std::optional<std::string> HeaderSearch::resolveQuoted(const SourceManager& sourceManager,
                                                       FileID includingFile,
                                                       const std::string& includePath) const {
    const fs::path requested(includePath);
    std::error_code ec;

    if (requested.is_absolute()) {
        if (fs::exists(requested, ec)) {
            return fs::weakly_canonical(requested, ec).string();
        }
        return std::nullopt;
    }

    const fs::path currentFile(sourceManager.getPath(includingFile));
    const fs::path currentDir = currentFile.has_parent_path() ? currentFile.parent_path() : fs::current_path();
    const fs::path relativeToCurrent = currentDir / requested;
    if (fs::exists(relativeToCurrent, ec)) {
        return fs::weakly_canonical(relativeToCurrent, ec).string();
    }

    for (const std::string& dir : includeDirs_) {
        const fs::path candidate = fs::path(dir) / requested;
        if (fs::exists(candidate, ec)) {
            return fs::weakly_canonical(candidate, ec).string();
        }
    }

    return std::nullopt;
}

} // namespace rhc
