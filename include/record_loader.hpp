#pragma once
#include <cassert>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

#include "record.hpp"

namespace files {
/*
 * Interface for loading files::Record objects from a binary file. It abstracts the way with which
 * the file is loaded in memory to enable transparent optimizations to the user.
 * TODO: memory mapped file
 */
class RecordLoader {
 public:
  class iterator {
   public:
    iterator() : loader_(nullptr) {
    }

    explicit iterator(RecordLoader* loader) : loader_(loader) {
      ++(*this);  // priming
    }

    auto operator*() -> Record& {
      return *current_record_;
    }
    auto operator->() -> Record* {
      return &(*current_record_);
    }
    auto operator++() -> iterator& {
      current_record_ = loader_->readNext();
      if (!current_record_) {
        loader_ = nullptr;
      }
      return *this;
    }
    auto operator++(int) -> iterator {
      iterator temp = *this;
      ++(*this);
      return temp;
    }
    friend auto operator==(const iterator& a, const iterator& b) -> bool {
      return a.loader_ == b.loader_;
    }
    friend auto operator!=(const iterator& a, const iterator& b) -> bool {
      return !(a == b);
    }

   private:
    RecordLoader* loader_;
    std::optional<Record> current_record_;
  };

  RecordLoader() = delete;
  RecordLoader(const std::filesystem::path&);
  auto readNext() -> std::optional<files::Record>;

  auto begin() -> iterator {
    return iterator(this);
  }
  auto end() -> iterator {
    return {};
  }

 private:
  std::ifstream filestream_;  // RAII, no need to manually close it
};

template <
    size_t BufferSize,
    typename Allocator = DefaultHeapAllocator<char>,
    typename RecordType = files::Record>
class BufferedRecordLoader {
 public:
  BufferedRecordLoader() = delete;
  BufferedRecordLoader(const std::filesystem::path&, bool = false);
  auto readNext(Allocator&) -> std::optional<RecordType>;
  auto bytesRemaining() const -> std::streamsize;
  auto close() -> void;
  auto eof() -> bool {
    return eof_;
  }
  auto prefetching() -> bool {
    // A prefetch is "in-flight" if the future is valid.
    return prefetch_future_.valid();
  }

 private:
  auto prefetch() -> std::streamsize;
  auto primeBuffer() -> void;
  auto fillPrefetchBuffer() -> std::streamsize;

  std::ifstream filestream_;
  std::array<char, BufferSize> buffer_;
  std::array<char, BufferSize> prefetch_buffer_;
  size_t buffer_size_{0};
  size_t current_pos_{0};
  bool async_prefetch_{false};
  bool eof_{false};
  std::future<void> initial_buffer_read_;
  std::future<std::streamsize> prefetch_future_;
  size_t prefetch_buffer_size_{0};
  size_t prefetch_buffer_pos_{0};
};

}  // namespace files

template <size_t BufferSize, typename Allocator, typename RecordType>
files::BufferedRecordLoader<BufferSize, Allocator, RecordType>::BufferedRecordLoader(
    const std::filesystem::path& path, bool async_prefetch
)
    : filestream_{path, std::ios::binary}, async_prefetch_{async_prefetch} {
  if (!filestream_.is_open()) {
    throw std::logic_error(std::format("Could not open file {}", path.string()));
  }
  if (async_prefetch_) {
    initial_buffer_read_ = std::async(std::launch::async, &BufferedRecordLoader::primeBuffer, this);
  } else {
    primeBuffer();
  }
}

template <size_t BufferSize, typename Allocator, typename RecordType>
auto files::BufferedRecordLoader<BufferSize, Allocator, RecordType>::primeBuffer() -> void {
  filestream_.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
  buffer_size_ = filestream_.gcount();
}

template <size_t BufferSize, typename Allocator, typename RecordType>
auto files::BufferedRecordLoader<BufferSize, Allocator, RecordType>::fillPrefetchBuffer()
    -> std::streamsize {
  if (eof_) {
    return 0;
  }
  if (prefetch_buffer_pos_ == 0 && prefetch_buffer_size_ == prefetch_buffer_.size()) {
    return 0;
  }

  // Shift remaining data to the left of the prefetch buffer.
  auto remaining = prefetch_buffer_size_ - prefetch_buffer_pos_;
  if (prefetch_buffer_pos_ > 0) {
    std::memcpy(
        prefetch_buffer_.data(), prefetch_buffer_.data() + prefetch_buffer_pos_, remaining
    );
    prefetch_buffer_size_ = remaining;
    prefetch_buffer_pos_ = 0;
  }

  assert(prefetch_buffer_pos_ == 0);

  filestream_.read(
      prefetch_buffer_.data() + remaining,
      static_cast<std::streamsize>(prefetch_buffer_.size()) - remaining
  );
  prefetch_buffer_size_ += filestream_.gcount();
  return prefetch_buffer_size_;
}

template <size_t BufferSize, typename Allocator, typename RecordType>
auto files::BufferedRecordLoader<BufferSize, Allocator, RecordType>::bytesRemaining() const
    -> std::streamsize {
  return buffer_size_ - current_pos_;
}

template <size_t BufferSize, typename Allocator, typename RecordType>
auto files::BufferedRecordLoader<BufferSize, Allocator, RecordType>::prefetch() -> std::streamsize {
  if (buffer_size_ == buffer_.size() && current_pos_ == 0) {
    return 0;
  }

  auto remaining = bytesRemaining();

  // Shift remaining data to the left of the active buffer.
  if (current_pos_ > 0) {
    std::memcpy(buffer_.data(), buffer_.data() + current_pos_, remaining);
    buffer_size_ = remaining;
    current_pos_ = 0;
  }

  std::streamsize bytes_read_from_prefetch = 0;

  // Check if a prefetch operation was in flight.
  if (prefetch_future_.valid()) {
    // Wait for the async read to finish and get the number of bytes read.
    prefetch_future_.get();
    bytes_read_from_prefetch = prefetch_buffer_size_ - prefetch_buffer_pos_;

    if (bytes_read_from_prefetch > 0) {
      // Determine how much data to copy from the prefetch buffer.
      size_t bytes_to_copy = std::min(
          static_cast<size_t>(bytes_read_from_prefetch), buffer_.size() - bytesRemaining()
      );

      // Copy from the prefetch buffer to the active buffer.
      std::memcpy(
          buffer_.data() + bytesRemaining(),
          prefetch_buffer_.data() + prefetch_buffer_pos_,
          bytes_to_copy
      );
      buffer_size_ += bytes_to_copy;
      prefetch_buffer_pos_ += bytes_to_copy;
    }
  } else if (!eof_) {
    // Fallback to synchronous read if no prefetch was initiated.
    filestream_.read(buffer_.data() + bytesRemaining(), buffer_.size() - bytesRemaining());
    bytes_read_from_prefetch = filestream_.gcount();
    buffer_size_ += bytes_read_from_prefetch;
    assert(buffer_size_ <= buffer_.size());
  }

  if (bytes_read_from_prefetch == 0) {
    eof_ = true;
  }

  return bytes_read_from_prefetch;
}

template <size_t BufferSize, typename Allocator, typename RecordType>
auto files::BufferedRecordLoader<BufferSize, Allocator, RecordType>::readNext(Allocator& allocator)
    -> std::optional<RecordType> {
  if (!filestream_.is_open()) {
    throw std::logic_error("Input stream not open");
  }

  if (async_prefetch_ && initial_buffer_read_.valid()) {
    initial_buffer_read_.get();
  }

  // If the first buffer does not fully contain the record's header, refill it.
  std::streamsize header_size = sizeof(uint64_t) + sizeof(uint32_t);
  if (header_size > bytesRemaining()) {
    if (!prefetch() && header_size >= bytesRemaining()) {
      return {};
    }
  }

  uint64_t key{0};
  uint32_t p_len{0};

  std::memcpy(&key, buffer_.data() + current_pos_, sizeof(uint64_t));
  current_pos_ += sizeof(uint64_t);
  assert(current_pos_ < buffer_size_);

  std::memcpy(&p_len, buffer_.data() + current_pos_, sizeof(uint32_t));
  current_pos_ += sizeof(uint32_t);
  assert(current_pos_ <= buffer_size_);

  if (p_len > bytesRemaining()) {
    if (!prefetch() && p_len > bytesRemaining()) {
      throw std::logic_error("Payload size bigger than buffer's");
    }
  }

  auto payload = allocator.alloc(p_len);
  assert(payload.size() == p_len);

  if (p_len > 0) {
    std::memcpy(payload.data(), buffer_.data() + current_pos_, p_len);
    current_pos_ += p_len;
    assert(current_pos_ <= buffer_size_);
  } else if (p_len == 0) {
    throw std::logic_error("Record length must be positive");
  }

  // After successfully reading a record, check if we need to launch a background read.
  if (async_prefetch_ && !prefetch_future_.valid() && (bytesRemaining() < BufferSize / 2) && bytesRemaining()) {
    if (!eof_) {
      prefetch_future_ = std::async(
          std::launch::async, &BufferedRecordLoader::fillPrefetchBuffer, this
      );
    }
  }

  return std::make_optional<RecordType>(key, std::move(payload));
}

template <size_t BufferSize, typename Allocator, typename RecordType>
auto files::BufferedRecordLoader<BufferSize, Allocator, RecordType>::close() -> void {
  filestream_.close();
}
