#pragma once
#include <string>
#include <vector>

namespace Bluetooth {
    void init();
    void setTree(const std::vector<uint8_t>& tree);
}
