#include <algorithm>
#include <ff/ff.hpp>
//
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <filesystem>
#include <functional>
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
    return EOS;
  }

 private:
  std::filesystem::path path_;
};

struct Collector : ff::ff_node_t<files::Record, std::vector<files::Record>> {
  Collector(size_t batch_size)
      : batch_size_{batch_size}, batch_{std::make_unique<std::vector<files::Record>>()} {
    batch_->reserve(batch_size);
  }

  // Send last batch even if not full
  auto eosnotify(ssize_t) -> void override {
    if (!batch_->empty()) {
      this->ff_send_out(batch_.release());
    }
  }

  auto svc(files::Record* record) -> std::vector<files::Record>* override {
    if (record == nullptr) {
      return GO_ON;
    }
    batch_->emplace_back(std::move(*record));
    if (batch_->size() == batch_size_) {
      auto batch_ptr = batch_.release();
      batch_ = std::make_unique<std::vector<files::Record>>();
      return batch_ptr;
    }
    return GO_ON;
  }

 private:
  size_t batch_size_;
  std::unique_ptr<std::vector<files::Record>> batch_;
};

struct BatchPrinter : ff::ff_node_t<std::vector<files::Record>, void> {
  auto svc(std::vector<files::Record>* batch_ptr) -> void* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::make_unique<std::vector<files::Record>>(std::move(*batch_ptr));
    std::cout << *batch << std::endl;
    std::cout << "Length: " << batch->size() << std::endl;
    return GO_ON;
  }
};

auto main() -> int {
  constexpr int num_records = 31;
  constexpr uint32_t max_payload_length = 8;
  constexpr uint32_t seed = 42;
  auto path = std::filesystem::path("test_file.bin");
  files::generateRandomFile(path, num_records, max_payload_length, seed);

  std::print(
      "File created succesfully at {}/{}\n", std::filesystem::current_path().string(), path.string()
  );
  auto records = files::readFile(path);

  std::cout << "Original: " << records << std::endl;
  std::cout << "Length: " << records.size() << std::endl;
  std::ranges::sort(records, std::ranges::less());
  // std::cout << "Sorted: " << records << std::endl;

  constexpr size_t batch_size = 5;
  auto emitter = Emitter(path);
  auto collector = Collector(batch_size);
  auto printer = BatchPrinter();
  auto pipe = ff::ff_pipeline{};
  pipe.add_stage(emitter);
  pipe.add_stage(&collector);  // can't pass a Collector const reference because it's stateful
  pipe.add_stage(printer);

  pipe.run_and_wait_end();

  return 0;
}
