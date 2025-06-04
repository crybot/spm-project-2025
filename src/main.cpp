#include <ff/ff.hpp>
//
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <print>
#include <iostream>
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

struct Record {
  unsigned long key;
  uint32_t len;
  char payload[];
};

auto main() -> int {
  constexpr int NUM_RECORDS = 10;
  constexpr uint32_t MAX_PAYLOAD_LENGTH = 12; 
  constexpr uint32_t SEED = 42;
  files::generateRandomFile(std::filesystem::path("../test_file.bin"), NUM_RECORDS, MAX_PAYLOAD_LENGTH, SEED);
  std::cout << files::MINIMUM_PAYLOAD_LENGTH << std::endl;

  return 0;
}
