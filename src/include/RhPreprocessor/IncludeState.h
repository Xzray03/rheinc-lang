#pragma once

#include "RhCore/SourceManager.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace rhc {

class IncludeState {
public:
    bool isActive(const std::string& canonicalPath) const;
    bool alreadyIncluded(const std::string& canonicalPath) const;
    FileID includedFile(const std::string& canonicalPath) const;

    void pushActive(std::string canonicalPath, FileID file);
    std::string popActive();
    void markIncluded(std::string canonicalPath, FileID file);

private:
    std::vector<std::string> active_;
    std::vector<FileID> activeFiles_;
    std::unordered_map<std::string, FileID> included_;
};

} // namespace rhc
