#pragma once
#include <string>
#include <vector>

namespace Tree {
    void init();

    std::vector<uint8_t> getTree();
    void                 setTree(const std::vector<uint8_t>& new_tree);

    [[nodiscard]] uint8_t* tick() noexcept;

    [[nodiscard]] std::vector<uint8_t> listExternalTriggers() noexcept;
    void                               triggerExternalTrigger(uint8_t id) noexcept;
}
