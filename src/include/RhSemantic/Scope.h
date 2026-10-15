#pragma once

#include "RhSemantic/Symbol.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rhc {

class Scope {
public:
    explicit Scope(Scope* parent);

    Scope* parent() const { return parent_; }
    Symbol* lookup(std::string_view name) const;
    Symbol* lookupLocal(std::string_view name) const;
    bool insert(Symbol* symbol);
    // Overwrites any existing entry (including builtins). Returns true if a name was replaced.
    bool insertOrReplace(Symbol* symbol);
    Scope* createChild();

private:
    Scope* parent_;
    std::unordered_map<std::string, Symbol*> symbols_;
    std::vector<std::unique_ptr<Scope>> children_;
};

} // namespace rhc
