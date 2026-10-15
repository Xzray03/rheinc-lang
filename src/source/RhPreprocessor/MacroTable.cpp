#include "RhPreprocessor/MacroTable.h"

namespace rhc {

const MacroDefinition* MacroTable::find(std::string_view name) const {
    const auto it = macros_.find(std::string(name));
    return it == macros_.end() ? nullptr : &it->second;
}

bool MacroTable::define(const MacroDefinition& definition) {
    const std::string key(definition.name);
    if (macros_.contains(key)) return false;
    macros_.emplace(key, definition);
    return true;
}

bool MacroTable::undefine(std::string_view name) {
    return macros_.erase(std::string(name)) != 0;
}

bool MacroTable::isDefined(std::string_view name) const {
    return macros_.contains(std::string(name));
}

} // namespace rhc
