#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include "memory_arena.hpp"

namespace files {
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

struct RecordBatch {
  MemoryArena<char> arena;
  std::vector<RecordView> records;

  RecordBatch(size_t batch_size, size_t arena_size) : arena(arena_size) {
    records.reserve(batch_size);
  }

  auto totalBytes(size_t header_size = sizeof(uint64_t) + sizeof(uint32_t)) -> size_t {
    return arena.used() + records.size() * header_size;
  }
};

}  // namespace files
