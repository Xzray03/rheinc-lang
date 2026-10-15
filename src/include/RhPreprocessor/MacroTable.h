#pragma once

#include "RhPreprocessor/MacroDefinition.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace rhc {

class MacroTable {
public:
    const MacroDefinition* find(std::string_view name) const;
    bool define(const MacroDefinition& definition);
    bool undefine(std::string_view name);
    bool isDefined(std::string_view name) const;

private:
    std::unordered_map<std::string, MacroDefinition> macros_;
};

} // namespace rhc
