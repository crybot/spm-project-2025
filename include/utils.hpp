#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

#include "record.hpp"

namespace files {

constexpr uint32_t MINIMUM_PAYLOAD_LENGTH = 8;
constexpr uint32_t DEFAULT_SEED = 42;

auto generateRandomFile(const std::filesystem::path&, int, uint32_t, uint32_t = DEFAULT_SEED) -> void;
auto readFile(const std::filesystem::path&) -> std::vector<Record>;
auto temporaryFile() -> std::filesystem::path;
}  // namespace files

inline auto operator<<(std::ostream& os, const files::RecordView& record) -> std::ostream& {
  os << "(" << std::dec << record.key << ",";
  for (auto b : record.payload) {
    os << std::hex << std::showbase << static_cast<unsigned int>(b);
  }
  os << ")";
  return os << std::dec << std::noshowbase;
}

inline auto operator<<(std::ostream& os, const std::vector<files::RecordView>& records)
    -> std::ostream& {
  os << "[";
  for (auto record : records) {
    os << record;
  }
  os << "]";
  return os;
}

inline auto operator<<(std::ostream& os, const files::Record& record) -> std::ostream& {
  os << "(" << std::dec << record.key << ",";
  for (auto b : record.payload) {
    os << std::hex << std::showbase << static_cast<unsigned int>(b);
  }
  os << ")";
  return os << std::dec << std::noshowbase;
}

inline auto operator<<(std::ostream& os, const std::vector<files::Record>& records)
    -> std::ostream& {
  os << "[";
  for (auto record : records) {
    os << record;
  }
  os << "]";
  return os;
}
