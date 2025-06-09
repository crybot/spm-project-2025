#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace files {
class MemoryArena;

struct AbstractRecord {
  uint64_t key;
  auto inline operator==(const AbstractRecord& other) const {
    return key == other.key;
  }
  auto inline operator<=>(const AbstractRecord& other) const -> std::strong_ordering {
    return key <=> other.key;
  }
};

struct Record : public AbstractRecord {
  std::vector<char> payload;
};

struct RecordView : public AbstractRecord {
  std::span<char> payload;
};

}  // namespace files
