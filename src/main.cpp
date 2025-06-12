#include <ff/ff.hpp>
//
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <filesystem>
#include <fstream>
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
    // auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to decode batch", false);
    auto batch_record = std::make_unique<files::RecordBatch>(batch_size_, arena_size);

    while (auto record = record_loader.readNext(batch_record->arena)) {
      batch_record->records.emplace_back(*std::move(record));

      if (batch_record->records.size() == batch_size_) {
        // stop_watch.reset();
        this->ff_send_out(batch_record.release());

        batch_record = std::make_unique<files::RecordBatch>(batch_size_, arena_size);
      }
    }

    if (batch_record->records.size() > 0) {
      // stop_watch.reset();
      this->ff_send_out(batch_record.release());
    }
    return EOS;
  }

 private:
  std::filesystem::path path_;
  size_t batch_size_;
  size_t payload_length_;
};

struct BatchSorter : ff::ff_node_t<files::RecordBatch, files::RecordBatch> {
  auto svc(files::RecordBatch* batch_ptr) -> files::RecordBatch* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::unique_ptr<files::RecordBatch>(batch_ptr);

    // auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to sort batch");
    std::ranges::sort(batch_ptr->records, std::ranges::less());
    return batch.release();
  }
};

struct BatchWriter : ff::ff_node_t<files::RecordBatch, void> {
  auto svc(files::RecordBatch* batch_ptr) -> void* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::unique_ptr<files::RecordBatch>(batch_ptr);
    auto bytes_to_write = batch->totalBytes(HEADER_SIZE);
    auto out_buffer = std::vector<char>(bytes_to_write);
    out_buffer.resize(bytes_to_write);
    
    auto temp_file = files::temporaryFile();
    auto out_file = std::ofstream(temp_file, std::ios::binary);

    if (!out_file) {
      throw std::logic_error("Could not open temporary file");
    }

    auto out_ptr = out_buffer.data();
    for (auto& record : batch->records) {
      uint64_t key = record.key;
      uint32_t len = record.payload.size();

      memcpy(out_ptr, &record.key, sizeof(key));
      out_ptr += sizeof(record.key);

      memcpy(out_ptr, &len, sizeof(len));
      out_ptr += sizeof(len);

      memcpy(out_ptr, record.payload.data(), len);
      out_ptr += len;
    }

    out_file.write(out_buffer.data(), bytes_to_write);
    std::println("Written {} bytes to {}", bytes_to_write, temp_file.string());

    return GO_ON;
  }

  static const size_t HEADER_SIZE = sizeof(uint64_t) + sizeof(uint32_t);
};

auto main(int argc, char* argv[]) -> int {
  const size_t batch_size = argc > 1 ? std::stoi(argv[1]) : 100'000;
  const size_t num_workers = argc > 2 ? std::stoi(argv[2]) : 1;
  auto path = std::filesystem::path(argv[3]);

  auto emitter = Emitter(path, batch_size, 1);
  auto writer = BatchWriter();
  auto pipe = ff::ff_pipeline{};

  auto farm = ff::ff_farm();
  auto workers = std::vector<ff::ff_node*>{};
  std::generate_n(std::back_inserter(workers), num_workers, []() {
    return new BatchSorter();
  });
  farm.set_scheduling_ondemand();
  farm.add_workers(workers);

  pipe.add_stage(&emitter);  // can't pass an Emitter const reference because it's stateful
  pipe.add_stage(farm);
  pipe.add_stage(writer);

  pipe.run_and_wait_end();

  return 0;
}
