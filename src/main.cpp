#include <ff/ff.hpp>
//
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <ostream>
#include <print>
#include <thread>
#include <utility>

#include "record.hpp"
#include "record_loader.hpp"
#include "stop_watch.hpp"
#include "utils.hpp"
#include "memory_arena.hpp"

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

struct Emitter : ff::ff_node_t<files::RecordBatch> {
  Emitter() = delete;
  Emitter(std::filesystem::path path, size_t batch_size, size_t expected_payload_length = 8)
      : path_{std::move(path)}, batch_size_{batch_size}, payload_length_{expected_payload_length} {
  }

  auto svc(files::RecordBatch*) -> files::RecordBatch* override {
    auto const arena_size = batch_size_ * payload_length_;
    auto record_loader = files::BufferedRecordLoader<1024 * 1024>(path_);
    auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to decode batch", false);
    auto batch_record = std::make_unique<files::RecordBatch>(batch_size_, arena_size);

    while (auto record = record_loader.readNext(batch_record->arena)) {
      batch_record->records.emplace_back(*std::move(record));

      if (batch_record->records.size() == batch_size_) {
        stop_watch.reset();
        this->ff_send_out(batch_record.release());

        batch_record = std::make_unique<files::RecordBatch>(batch_size_, arena_size);
      }
    }

    if (batch_record->records.size() > 0) {
      stop_watch.reset();
      this->ff_send_out(batch_record.release());
    }
    return EOS;
  }

 private:
  std::filesystem::path path_;
  size_t batch_size_;
  size_t payload_length_;
};

struct BatchPrinter : ff::ff_node_t<files::RecordBatch, void> {
  auto svc(files::RecordBatch* batch_ptr) -> void* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::unique_ptr<files::RecordBatch>(batch_ptr);

    auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to sort batch");
    std::ranges::sort(batch_ptr->records, std::ranges::less());

    return GO_ON;
  }
};

auto main(int argc, char* argv[]) -> int {
  auto path = std::filesystem::path("medium.bin");

  const size_t batch_size = argc > 1 ? std::stoi(argv[1]) : 100'000;
  const size_t num_workers = argc > 2 ? std::stoi(argv[2]) : 1;

  // auto records = files::readFile(path);
  // std::cout << records << std::endl;
  // std::cout << "Length: " << records.size() << std::endl;

  auto emitter = Emitter(path, batch_size, 1);
  auto pipe = ff::ff_pipeline{};

  auto farm = ff::ff_farm();
  auto workers = std::vector<ff::ff_node*>{};
  std::generate_n(std::back_inserter(workers), num_workers, []() {
    return new BatchPrinter();
  });
  farm.set_scheduling_ondemand();
  farm.add_workers(workers);

  pipe.add_stage(&emitter);  // can't pass an Emitter const reference because it's stateful
  pipe.add_stage(farm);

  pipe.run_and_wait_end();

  return 0;
}
