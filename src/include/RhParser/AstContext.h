#pragma once

#include "RhParser/AstNode.h"

#include <deque>
#include <memory>
#include <utility>

namespace rhc {

class AstContext {
public:
    template<typename Node, typename... Args>
    Node* create(Args&&... args) {
        auto node = std::make_unique<Node>(std::forward<Args>(args)...);
        Node* result = node.get();
        nodes_.push_back(std::move(node));
        return result;
    }

private:
    std::deque<std::unique_ptr<AstNode>> nodes_;
};

} // namespace rhc
