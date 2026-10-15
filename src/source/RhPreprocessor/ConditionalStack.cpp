#include "RhPreprocessor/ConditionalStack.h"

namespace rhc {

void ConditionalStack::beginIf(bool condition, bool parentActive, SourceLocation loc) {
    ConditionalEntry entry;
    entry.parentActive = parentActive;
    entry.active = parentActive && condition;
    entry.anyTaken = entry.active;
    entry.loc = loc;
    stack_.push_back(entry);
}

bool ConditionalStack::beginElseIf(bool condition, SourceLocation loc) {
    if (stack_.empty() || stack_.back().seenElse) return false;
    ConditionalEntry& entry = stack_.back();
    entry.active = entry.parentActive && !entry.anyTaken && condition;
    entry.anyTaken = entry.anyTaken || entry.active;
    entry.loc = loc;
    return true;
}

bool ConditionalStack::beginElse(SourceLocation loc) {
    if (stack_.empty() || stack_.back().seenElse) return false;
    ConditionalEntry& entry = stack_.back();
    entry.seenElse = true;
    entry.active = entry.parentActive && !entry.anyTaken;
    entry.anyTaken = true;
    entry.loc = loc;
    return true;
}

bool ConditionalStack::end(SourceLocation loc) {
    (void)loc;
    if (stack_.empty()) return false;
    stack_.pop_back();
    return true;
}

bool ConditionalStack::empty() const { return stack_.empty(); }

bool ConditionalStack::isCurrentActive() const {
    return stack_.empty() || stack_.back().active;
}

bool ConditionalStack::currentParentActive() const {
    if (stack_.empty()) return true;
    bool active = true;
    for (const ConditionalEntry& entry : stack_) {
        active = active && entry.active;
    }
    return active;
}

bool ConditionalStack::currentSeenElse() const {
    return !stack_.empty() && stack_.back().seenElse;
}

const ConditionalEntry& ConditionalStack::current() const {
    return stack_.back();
}

} // namespace rhc
