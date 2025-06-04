#pragma once
#include <cstdint>
#include <filesystem>

namespace files {
constexpr uint32_t MINIMUM_PAYLOAD_LENGTH = 8;
constexpr uint32_t DEFAULT_SEED = 42;

auto generateRandomFile(std::filesystem::path, int, uint32_t, uint32_t = DEFAULT_SEED) -> void;
}  // namespace files
