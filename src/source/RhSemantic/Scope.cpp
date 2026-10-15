#include "RhSemantic/Scope.h"

namespace rhc {

Scope::Scope(Scope* parent) : parent_(parent) {}

Symbol* Scope::lookup(std::string_view name) const {
    const Scope* scope = this;
    while (scope) {
        if (Symbol* symbol = scope->lookupLocal(name)) return symbol;
        scope = scope->parent();
    }
    return nullptr;
}

Symbol* Scope::lookupLocal(std::string_view name) const {
    const auto it = symbols_.find(std::string(name));
    return it == symbols_.end() ? nullptr : it->second;
}

bool Scope::insert(Symbol* symbol) {
    if (!symbol) return false;
    const auto [it, inserted] = symbols_.emplace(symbol->name, symbol);
    return inserted;
}

bool Scope::insertOrReplace(Symbol* symbol) {
    const bool replaced = symbols_.find(symbol->name) != symbols_.end();
    symbols_[symbol->name] = symbol;
    return replaced;
}

Scope* Scope::createChild() {
    children_.push_back(std::make_unique<Scope>(this));
    return children_.back().get();
}

} // namespace rhc
