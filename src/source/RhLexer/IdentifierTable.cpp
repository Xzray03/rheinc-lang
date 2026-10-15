#include "RhLexer/IdentifierTable.h"

#include "RhLexer/KeywordTable.h"

#include <functional>

namespace rhc {

IdentifierInfo& IdentifierTable::intern(std::string_view name) {
    if (auto it = map_.find(name); it != map_.end()) {
        return *it->second;
    }

    auto stored = std::make_unique<std::string>(name);
    const std::string_view stableName(*stored);

    TokenKind kind = TokenKind::Identifier;
    ContextualKeywordID contextual = ContextualKeywordID::None;
    if (const KeywordEntry* entry = KeywordTable::lookup(stableName)) {
        kind = entry->tokenKind;
        contextual = entry->contextualID;
    }

    const std::uint32_t hash = static_cast<std::uint32_t>(std::hash<std::string_view>{}(stableName));
    auto info = std::make_unique<IdentifierInfo>(stableName, kind, contextual, hash);
    IdentifierInfo& result = *info;
    storage_.push_back(std::move(stored));
    map_.emplace(stableName, std::move(info));
    return result;
}

const IdentifierInfo* IdentifierTable::lookup(std::string_view name) const {
    if (auto it = map_.find(name); it != map_.end()) {
        return it->second.get();
    }
    return nullptr;
}

} // namespace rhc
