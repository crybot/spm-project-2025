#pragma once
#include <cstdint>
#include <vector>

namespace files {
struct Record {
  uint64_t key;
  std::vector<char> payload;
};
}  // namespace files
