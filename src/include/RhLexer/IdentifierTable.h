#pragma once

#include "RhLexer/IdentifierInfo.h"

#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace rhc {

class IdentifierTable {
public:
    IdentifierInfo& intern(std::string_view name);
    const IdentifierInfo* lookup(std::string_view name) const;

private:
    std::deque<std::unique_ptr<std::string>> storage_;
    std::unordered_map<std::string_view, std::unique_ptr<IdentifierInfo>> map_;
};

} // namespace rhc
