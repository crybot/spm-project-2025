#pragma once
#include <cstdint>
#include <vector>

namespace files {
struct Record {
  uint64_t key;
  std::vector<char> payload;

  auto inline operator==(const Record& other) const {
    return key == other.key;
  }
  auto inline operator<=>(const Record& other) const -> std::strong_ordering {
    return key <=> other.key;
  }
};

}  // namespace files
