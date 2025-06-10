#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace files {
class MemoryArena;

// template <typename C = std::vector<char>>
struct Record {
  uint64_t key;
  std::vector<char> payload;

  auto inline operator==(const Record& other) const -> bool {
    return key == other.key;
  }
  auto inline operator<=>(const Record& other) const -> std::strong_ordering {
    return key <=> other.key;
  }
};

struct RecordView {
  uint64_t key;
  std::span<char> payload;

  auto inline operator==(const RecordView& other) const -> bool {
    return key == other.key;
  }
  auto inline operator<=>(const RecordView& other) const -> std::strong_ordering {
    return key <=> other.key;
  }
};

}  // namespace files
