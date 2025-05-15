#pragma once
#include <string>

namespace Tree {
    void                   init();
    std::string            getTree();
    void                   setTree(const std::string& tree);
    [[nodiscard]] uint8_t* tick() noexcept;
}
