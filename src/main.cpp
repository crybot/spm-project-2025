#include <ff/ff.hpp>
//
#include <cstdio>
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

#include "memory_arena.hpp"
#include "record.hpp"
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

// The Emitter node reads Records sequentially from a file and collects them into a contiguous batch
// to exploit data locality. To improve throughput, the RecordLoader uses an internal buffer to
// minimize read() system calls and employs a different MemoryArena per batch. This both drastically
// reduces heap allocations when creating Records (because of the variable payload size) and
// mitigates intra-batch false sharing issues because batches will live on different memory regions.
// Nonetheless, the IO here is the main bottleneck and, since we cannot assume the entire dataset
// can be stored in memory, much of the improvements are shadowed by read (and write) operations.
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
    std::ranges::sort(batch->records, std::ranges::less());
    return batch.release();
  }
};

struct Collector : ff::ff_node_t<files::RecordBatch> {
  auto svc(files::RecordBatch* batch_ptr) -> files::RecordBatch* override {
    return batch_ptr;
  }
};

struct BatchWriter : ff::ff_node_t<files::RecordBatch, void> {
  auto svc(files::RecordBatch* batch_ptr) -> void* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::unique_ptr<files::RecordBatch>(batch_ptr);
    auto bytes_to_write = batch->totalBytes(HEADER_SIZE);

    auto temp_file = files::temporaryFile();
    auto out_file = std::ofstream(temp_file, std::ios::binary);

    if (!out_file) {
      throw std::logic_error("Could not open temporary file");
    }

    auto out_buffer = std::vector<char>(bytes_to_write);
    auto out_stream = std::span<char>(out_buffer);
    for (const auto& record : batch->records) {
      files::encodeRecord(record, out_stream);
    }
    assert(out_stream.empty());
    out_file.write(out_buffer.data(), bytes_to_write);
    std::println("Written {} bytes to {}", bytes_to_write, temp_file.string());

    return new std::filesystem::path(std::move(temp_file));
  }


  static const size_t HEADER_SIZE = sizeof(uint64_t) + sizeof(uint32_t);
};

struct ResultCollector : ff::ff_node_t<std::filesystem::path, void> {
  std::vector<std::filesystem::path> sorted_files{};

  auto svc(std::filesystem::path* path) -> void* override {
    if (path) {
      sorted_files.emplace_back(std::move(*path));
    }
    return GO_ON;
  }
};

struct FileMerger : ff::ff_node {
  FileMerger(const std::vector<std::filesystem::path>&& files) : files_(files) {
  }
  auto svc(void*) -> void* override {
    for (auto& file : files_) {
      // TODO:
      std::println("Deleting temporary file {}", file.string());
      std::filesystem::remove(file);
    }
    return EOS;
  }

 private:
  std::vector<std::filesystem::path> files_;
};

auto main(int argc, char* argv[]) -> int {
  const size_t batch_size = argc > 1 ? std::stoi(argv[1]) : 100'000;
  const size_t num_workers = argc > 2 ? std::stoi(argv[2]) : 1;
  auto path = std::filesystem::path(argv[3]);

  // Emitter stage: reads file and batches records
  auto sorting_pipe = ff::ff_pipeline{};
  auto emitter = Emitter(path, batch_size, 1);
  sorting_pipe.add_stage(&emitter);  // can't pass an Emitter const reference because it's stateful

  // Sorting stage: farm of workers that sorts batches
  auto sorter_farm = ff::ff_farm();
  auto sorter_workers = std::vector<ff::ff_node*>{};
  std::generate_n(std::back_inserter(sorter_workers), num_workers, []() {
    return new BatchSorter();
  });
  sorter_farm.add_workers(sorter_workers);
  sorter_farm.add_collector(new Collector); // Collects sorted batches and forwards them
  sorting_pipe.add_stage(sorter_farm);

  // Writing stage: writes sorted batches into temporary files
  auto writer_farm = ff::ff_farm();
  auto writer_workers = std::vector<ff::ff_node*>{};
  std::generate_n(std::back_inserter(writer_workers), 1, []() {
    return new BatchWriter();
  });
  writer_farm.add_workers(writer_workers);
  sorting_pipe.add_stage(writer_farm);

  // Collection stage: collects temporary files into a vector
  auto result_collector = new ResultCollector();
  sorting_pipe.add_stage(result_collector);

  // Wait for each batch to be written on disk
  sorting_pipe.run_and_wait_end(); // This acts as a barrier

  auto sorted_files = std::move(result_collector->sorted_files);

  // Merging pipeline
  auto merging_pipe = ff::ff_pipeline{};
  auto file_merger = FileMerger(std::move(sorted_files));
  merging_pipe.add_stage(file_merger);
  merging_pipe.run_and_wait_end();

  return 0;
}
