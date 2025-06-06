#include <ff/ff.hpp>
//
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <filesystem>
#include <iostream>
#include <memory>
#include <print>
#include <utility>

#include "record_loader.hpp"
#include "utils.hpp"

/*
 *                                        SPM PROJECT
 * Design and implement a scalable MergeSort for an N-record file where each record can have a
 * different size in the range [8, PAYLOAD_MAX], where PAYLOAD_MAX is a constant value. A possible
 * C-like representation of one record is as follows: The payload is a blob of bytes arbitrarily
 * initialized. The key field of the record is the only comparison field. You should not assume that
 * the file containing the records to sort can be entirely loaded into the memory of one node. Your
 * solution must work even if the file size exceeds the RAM of a node. Consider the maximum
 * available RAM of a node to be 32 GB. Tasks
 *
 * Tasks:
 * 1. Single-node versions (shared-memory)
 * Implement two versions, one with FastFlow parallel building blocks (i.e., farm / pipeline/
 * all-to-all) and one with OpenMP pragmas. Both must produce identical output. Sequential sorting
 * can be implemented leveraging std::sort or equivalent C++ standard library calls
 *
 * Notes:
 * - Only use FF building blocks (no high-level patterns)
 * - There's just a single file, which does not fit in memory:
 *    + Memory mapped file?
 *    + Moving around range references?
 * - We can assume 32GB of ram
 *
 */

struct Emitter : ff::ff_node_t<files::Record> {
  Emitter() = delete;
  Emitter(std::filesystem::path path) : path_{std::move(path)} {
  }
  auto svc(files::Record*) -> files::Record* override {
    auto record_loader = files::RecordLoader(path_);
    for (auto record : record_loader) {
      this->ff_send_out(std::make_unique<files::Record>(std::move(record)).release());
    }
    return GO_ON;
  }

 private:
  std::filesystem::path path_;
};

auto main() -> int {
  constexpr int num_records = 10;
  constexpr uint32_t max_payload_length = 12;
  constexpr uint32_t seed = 42;
  auto path = std::filesystem::path("test_file.bin");
  files::generateRandomFile(path, num_records, max_payload_length, seed);

  std::print(
      "File created succesfully at {}/{}\n", std::filesystem::current_path().string(), path.string()
  );
  auto records = files::readFile(path);

  std::cout << records << std::endl;

  return 0;
}
