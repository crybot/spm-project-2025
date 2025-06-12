#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
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

template<typename T>
concept IsRecord = requires(const T& r) {
    { r.key } -> std::same_as<const uint64_t&>;
    { r.payload.size() } -> std::convertible_to<size_t>;
    { r.payload.data() } -> std::same_as<char*>;
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

template<IsRecord T>
inline auto encodeRecord(const T& record, std::span<char>& out_stream) -> void {
  const uint64_t key = record.key;
  const uint32_t len = record.payload.size();
  const auto total_size = sizeof(key) + sizeof(len) + len;

  if (out_stream.size() < total_size) {
    throw std::logic_error(std::format(
        "Not enough space left in out_stream for encoding the record of size {}", total_size
    ));
  }
  memcpy(out_stream.data(), &key, sizeof(key));
  memcpy(out_stream.data() + sizeof(key), &len, sizeof(len));
  if (len > 0) {
    memcpy(out_stream.data() + sizeof(key) + sizeof(len), record.payload.data(), len);
  }
  out_stream = out_stream.subspan(total_size);
}

}  // namespace files
