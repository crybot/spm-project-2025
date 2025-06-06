#pragma once
#include <filesystem>
#include <fstream>

#include "record.hpp"

namespace files {
/*
 * Interface for loading files::Record objects from a binary file. It abstracts the way with the
 * file is loaded in memory to enable transparent optimizations in the future.
 * TODO: memory mapped file, prefetch buffer of file into memory
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
  auto prefetch(size_t) -> bool;

  auto begin() -> iterator {
    return iterator(this);
  }
  auto end() -> iterator {
    return {};
  }

 private:
  std::vector<files::Record> buffer_;
  std::ifstream filestream_;  // RAII, no need to manually close it
};
}  // namespace files
