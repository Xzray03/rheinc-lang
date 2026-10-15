#pragma once

#include "RhCore/SourceLocation.h"

#include <vector>

namespace rhc {

struct ConditionalEntry {
    bool anyTaken = false;
    bool active = false;
    bool seenElse = false;
    bool parentActive = true;
    SourceLocation loc = 0;
};

class ConditionalStack {
public:
    void beginIf(bool condition, bool parentActive, SourceLocation loc);
    bool beginElseIf(bool condition, SourceLocation loc);
    bool beginElse(SourceLocation loc);
    bool end(SourceLocation loc);

    bool empty() const;
    bool isCurrentActive() const;
    bool currentParentActive() const;
    bool currentSeenElse() const;
    const ConditionalEntry& current() const;

private:
    std::vector<ConditionalEntry> stack_;
};

} // namespace rhc
