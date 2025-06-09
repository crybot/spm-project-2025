#pragma once
#include <cassert>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>

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
  virtual auto readNext() -> std::optional<files::Record>;

  auto begin() -> iterator {
    return iterator(this);
  }
  auto end() -> iterator {
    return {};
  }

 protected:
  std::ifstream filestream_;  // RAII, no need to manually close it
};

template <size_t BufferSize>
class BufferedRecordLoader : public RecordLoader {
 public:
  BufferedRecordLoader() = delete;
  BufferedRecordLoader(const std::filesystem::path&);
  auto readNext() -> std::optional<files::Record> override;
  auto bytesRemaining() const -> std::streamsize;

 private:
  auto prefetch() -> std::streamsize;
  std::array<char, BufferSize> buffer_;
  std::size_t buffer_size_;
  std::size_t current_pos_;
};

}  // namespace files


template <size_t BufferSize>
files::BufferedRecordLoader<BufferSize>::BufferedRecordLoader(const std::filesystem::path& path)
    : RecordLoader(path), buffer_{}, buffer_size_{0}, current_pos_{0} {
  filestream_.read(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
  buffer_size_ = filestream_.gcount();
}

template <size_t BufferSize>
auto files::BufferedRecordLoader<BufferSize>::bytesRemaining() const -> std::streamsize {
  return buffer_size_ - current_pos_;
}

template <size_t BufferSize>
auto files::BufferedRecordLoader<BufferSize>::prefetch() -> std::streamsize {
  // If the first buffer is already full, do nothing
  if (buffer_size_ == buffer_.size() && current_pos_ == 0) {
    return 0;
  }

  std::streamsize bytes_read = 0;
  // Shift remaining data to the left of the buffer
  if (current_pos_ > 0) {
    auto remaining = bytesRemaining();
    std::memmove(buffer_.data(), buffer_.data() + current_pos_, remaining);
    buffer_size_ = remaining;
    current_pos_ = 0;

    // Try to fill the buffer
    filestream_.read(
        buffer_.data() + bytesRemaining(), buffer_.size() - bytesRemaining()
    );
    bytes_read = filestream_.gcount();
    buffer_size_ = bytes_read + remaining;
  }

  return bytes_read;
}

template <size_t BufferSize>
auto files::BufferedRecordLoader<BufferSize>::readNext() -> std::optional<files::Record> {
  uint64_t key{0};
  uint32_t p_len{0};

  // NOTE: We assume that a single record is at most contained within two adjacent buffers, that is if the current
  // buffer does not completely contain the record being read, then for sure the next buffer will fully contain it.
  // For example: 
  // - Current buffer:          [<consumed_data>...<KEY><P_LEN>]
  // - Shift data to the left:  [<KEY><P_LEN>...<uninitialized_data>]
  // - Read data from buffer:   [<KEY><P_LEN><PAYLOAD>...<fresh_data>]
  // By our assumptions <PAYLOAD> is now fully contained in the current buffer

  // If the first buffer does not fully contain the record's header, refill it
  std::streamsize header_size = sizeof(key) + sizeof(p_len);
  if (header_size > bytesRemaining()) {
    if (!prefetch() && header_size >= bytesRemaining()) {
      return {};
    }
  }

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

  auto payload = std::vector<char>(p_len);
  if (p_len > 0) {
    std::memcpy(payload.data(), buffer_.data() + current_pos_, p_len);
    current_pos_ += p_len;
    assert(current_pos_ <= buffer_size_);
  }
  else if (p_len == 0) {
    throw std::logic_error("Record length must be positive");
  }
  return std::make_optional<Record>(key, std::move(payload));
}
