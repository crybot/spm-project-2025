#include "utils.hpp"

#include <cstdint>
#include <format>
#include <fstream>
#include <ios>
#include <limits>
#include <print>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>
#include <atomic>
#include "record_loader.hpp"

auto files::generateRandomFile(
    const std::filesystem::path& path, int num_records, uint32_t max_payload_length, uint32_t seed
) -> void {
  /*
   * Write a binary file to the specificed `path` containing `num_records` with the following
   * format: <uint64_t:key><uint32_t:p_len>[<char:byte_1><char:byte_2>...<char:byte_{p_len}>]
   */
  if (num_records <= 0) {
    throw std::invalid_argument("num_records must be positive");
  }
  if (max_payload_length < files::MINIMUM_PAYLOAD_LENGTH) {
    throw std::invalid_argument(
        std::format("max_payload_length must be >= {}", files::MINIMUM_PAYLOAD_LENGTH)
    );
  }

  auto gen = std::mt19937{seed};
  auto len_distrib = std::uniform_int_distribution<uint64_t>{
      files::MINIMUM_PAYLOAD_LENGTH, max_payload_length
  };
  auto key_distrib = std::uniform_int_distribution<uint64_t>{
      0, std::numeric_limits<uint64_t>::max()
  };
  const std::vector<char> max_payload(max_payload_length, char{0});  // A string filled with zero-bytes

  auto outfile = std::ofstream(path, std::ios::binary);  // RAII object

  for (auto i = 0; i < num_records; i++) {
    uint32_t p_len = len_distrib(gen);
    uint64_t key = key_distrib(gen);
    outfile.write(reinterpret_cast<const char*>(&key), sizeof(key));
    outfile.write(reinterpret_cast<const char*>(&p_len), sizeof(p_len));
    outfile.write(max_payload.data(), p_len);
  }
}

auto files::readFile(const std::filesystem::path& path) -> std::vector<files::Record> {
  /*
   * Read a binary file from the specificed `path` containing records with the following
   * format: <uint64_t:key><uint32_t:p_len>[<char:byte_1><char:byte_2>...<char:byte_{p_len}>]
   *
   * return: std::vector<Record> containing all the decoded records.
   */
  auto records = std::vector<Record>{};
  auto record_loader = files::RecordLoader(path);

  while (auto record = record_loader.readNext()) {
    records.emplace_back(*std::move(record));
  }

  return records;
}

auto files::temporaryFile() -> std::filesystem::path {
 static std::atomic<int> counter = 0;

 std::stringstream ss;
 ss << std::this_thread::get_id();
 std::string filename = "sorted_run_" + ss.str() + "_" + std::to_string(counter++) + ".bin";

 return std::filesystem::temp_directory_path() / filename;
}
