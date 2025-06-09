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

#include "record_loader.hpp"
#include "stop_watch.hpp"
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

struct Emitter : ff::ff_node_t<std::vector<files::Record>> {
  Emitter() = delete;
  Emitter(std::filesystem::path path, size_t batch_size)
      : path_{std::move(path)},
        batch_size_{batch_size},
        batch_{std::make_unique<std::vector<files::Record>>()} {
    batch_->reserve(batch_size);
  }

  auto svc(std::vector<files::Record>*) -> std::vector<files::Record>* override {
    auto record_loader = files::BufferedRecordLoader<4UL * 1024 * 1024>(path_);

    auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to decode batch");
    for (auto record : record_loader) {
      batch_->emplace_back(std::move(record));
      if (batch_->size() == batch_size_) {
        stop_watch.reset();
        this->ff_send_out(batch_.release());
        batch_ = std::make_unique<std::vector<files::Record>>();
        // batch_->reserve(batch_size_); // Somehow this makes things slower
      }
    }

    if (batch_->size() > 0) {
      stop_watch.reset();
      this->ff_send_out(batch_.release());
    }
    return EOS;
  }

 private:
  std::filesystem::path path_;
  size_t batch_size_;
  std::unique_ptr<std::vector<files::Record>> batch_;
};

struct BatchPrinter : ff::ff_node_t<std::vector<files::Record>, void> {
  auto svc(std::vector<files::Record>* batch_ptr) -> void* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::make_unique<std::vector<files::Record>>(std::move(*batch_ptr));
    std::ranges::sort(*batch, std::ranges::less());
    // std::this_thread::sleep_for(std::chrono::milliseconds(1));
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

  auto emitter = Emitter(path, batch_size);
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
