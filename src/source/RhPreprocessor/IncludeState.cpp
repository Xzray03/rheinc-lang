#include "RhPreprocessor/IncludeState.h"

#include <algorithm>

namespace rhc {

bool IncludeState::isActive(const std::string& canonicalPath) const {
    return std::find(active_.begin(), active_.end(), canonicalPath) != active_.end();
}

bool IncludeState::alreadyIncluded(const std::string& canonicalPath) const {
    return included_.contains(canonicalPath);
}

FileID IncludeState::includedFile(const std::string& canonicalPath) const {
    const auto it = included_.find(canonicalPath);
    return it == included_.end() ? FileID() : it->second;
}

void IncludeState::pushActive(std::string canonicalPath, FileID file) {
    active_.push_back(std::move(canonicalPath));
    activeFiles_.push_back(file);
}

std::string IncludeState::popActive() {
    std::string result = active_.back();
    active_.pop_back();
    activeFiles_.pop_back();
    return result;
}

void IncludeState::markIncluded(std::string canonicalPath, FileID file) {
    included_.emplace(std::move(canonicalPath), file);
}

} // namespace rhc
