#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cn {

// Stable, deterministic hex output used by -v and by the final annotated dump.
std::string hex_dump(const std::vector<std::uint8_t>& bytes);

} // namespace cn
