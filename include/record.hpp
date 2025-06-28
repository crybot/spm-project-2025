#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <istream>

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

template <typename T>
concept IsRecord = std::is_same_v<T, files::Record> || std::is_same_v<T, files::RecordView>;

struct ArenaBatch {
  MemoryArena<char> arena;
  std::vector<RecordView> records;

  ArenaBatch(size_t batch_size, size_t arena_size) : arena(arena_size) {
    records.reserve(batch_size);
  }

  auto totalBytes(size_t header_size = sizeof(uint64_t) + sizeof(uint32_t)) -> size_t {
    return arena.used() + records.size() * header_size;
  }
};

struct RecordBatch {
  std::vector<Record> records;
  size_t total_size{0};

  RecordBatch(size_t batch_size) {
    records.reserve(batch_size);
  }
};

template <IsRecord T>
inline auto encodeRecord(const T& record, std::span<char>& out_stream) -> size_t {
  const uint64_t key = record.key;
  const uint32_t len = record.payload.size();
  const auto total_size = sizeof(key) + sizeof(len) + len;

  if (out_stream.size() < total_size) {
    throw std::logic_error("Not enough space left in out_stream for encoding the record");
  }
  memcpy(out_stream.data(), &key, sizeof(key));
  memcpy(out_stream.data() + sizeof(key), &len, sizeof(len));
  if (len > 0) {
    memcpy(out_stream.data() + sizeof(key) + sizeof(len), record.payload.data(), len);
  }
  // Move span `total_size` bytes to the right: although counterintuitive, here `total_size` acts as
  // an offset
  out_stream = out_stream.subspan(total_size);
  return total_size;
}

struct HeapNode {
  // TODO: delete default constructor
  files::Record record;
  size_t loader_index;

  auto operator<=>(const HeapNode& other) const -> std::strong_ordering {
    return record <=> other.record;
  }
};

class MemoryViewStreambuf : public std::streambuf {
 public:
  MemoryViewStreambuf(const char* data, size_t size) {
    auto start_p = const_cast<char*>(data);
    this->setg(start_p, start_p, start_p + size);
  }
};

/**
 * An input stream that provides a non-owning, zero-copy view over
 * a contiguous block of memory. Emulates C++23's std::ispanstream.
 */
class MemoryViewInputStream : public std::istream {
 public:
  MemoryViewInputStream(char* data, size_t size) : std::istream(nullptr), sbuf_(data, size) {
    this->rdbuf(&sbuf_);
  }
  MemoryViewInputStream(const char* data, size_t size) : std::istream(nullptr), sbuf_(data, size) {
    this->rdbuf(&sbuf_);
  }

  explicit MemoryViewInputStream(std::span<char> s) : MemoryViewInputStream(s.data(), s.size()) {
  }
  explicit MemoryViewInputStream(std::vector<char>& v) : MemoryViewInputStream(v.data(), v.size()) {
  }

  explicit MemoryViewInputStream(std::span<const char> s)
      : MemoryViewInputStream(s.data(), s.size()) {
  }
  explicit MemoryViewInputStream(const std::vector<char>& v)
      : MemoryViewInputStream(v.data(), v.size()) {
  }

 private:
  MemoryViewStreambuf sbuf_;
};

}  // namespace files
