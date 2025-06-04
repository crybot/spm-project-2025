#include "utils.hpp"

#include <format>
#include <fstream>
#include <ios>
#include <random>
#include <stdexcept>

auto files::generateRandomFile(
    std::filesystem::path path, int num_records, uint32_t max_payload_length, uint32_t seed
) -> void {
  if (num_records <= 0) {
    throw std::invalid_argument("num_records must be positive");
  }
  if (max_payload_length < files::MINIMUM_PAYLOAD_LENGTH) {
    throw std::invalid_argument(
        std::format("max_payload_length must be >= {}", files::MINIMUM_PAYLOAD_LENGTH)
    );
  }

  auto gen = std::mt19937{seed};
  auto distrib = std::uniform_int_distribution<uint64_t>{
      files::MINIMUM_PAYLOAD_LENGTH, max_payload_length
  };

  std::string max_payload(max_payload_length, '0');

  auto outfile = std::ofstream(path, std::ios::binary); // RAII object

  for (auto i = 0; i < num_records; i++) {
    uint32_t p_len = distrib(gen);
    uint64_t key = distrib(gen);
    outfile.write(reinterpret_cast<const char*>(&key), sizeof(key));
    outfile.write(reinterpret_cast<const char*>(&p_len), sizeof(p_len));
    outfile.write(max_payload.data(), p_len);
  }
}
