#pragma once

#include "RhCore/SourceManager.h"

#include <optional>
#include <string>
#include <vector>

namespace rhc {

class HeaderSearch {
public:
    void addIncludeDirectory(std::string path);

    std::optional<std::string> resolveQuoted(const SourceManager& sourceManager,
                                             FileID includingFile,
                                             const std::string& includePath) const;

private:
    std::vector<std::string> includeDirs_;
};

} // namespace rhc
