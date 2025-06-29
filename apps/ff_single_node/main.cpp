#include <ff/ff.hpp>
//
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <ff/parallel_for.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>

#include "cli_parser.hpp"
#include "memory_arena.hpp"
#include "record.hpp"
#include "record_loader.hpp"
#include "stop_watch.hpp"
#include "utils.hpp"

/*
 *                                  SPM PROJECT (single node: FF)
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

static constexpr size_t header_size = sizeof(uint64_t) + sizeof(uint32_t);

// The Emitter node reads Records sequentially from a file and collects them into a contiguous batch
// to exploit data locality. To improve throughput, the RecordLoader uses an internal buffer to
// minimize read() system calls and employs a different MemoryArena per batch. This both drastically
// reduces heap allocations when creating Records and mitigates intra-batch false sharing issues
// because batches will live on different memory regions. Nonetheless, the IO here is the main
// bottleneck and, since we cannot assume the entire dataset can be stored in internal memory, much
// of the improvements are shadowed by read (and write) operations.
template<size_t BufferSize>
struct Emitter : ff::ff_node_t<files::ArenaBatch> {
  Emitter() = delete;
  Emitter(std::filesystem::path path, size_t batch_size, size_t expected_payload_length = 8)
      : path_{std::move(path)}, batch_size_{batch_size}, payload_length_{expected_payload_length} {
  }

  auto svc(files::ArenaBatch*) -> files::ArenaBatch* override {
    auto const arena_size = batch_size_ * payload_length_;
    auto record_loader = files::
        BufferedRecordLoader<BufferSize, MemoryArena<char>, files::RecordView>(path_);
    auto batch_record = std::make_unique<files::ArenaBatch>(batch_size_, arena_size);

    while (auto record = record_loader.readNext(batch_record->arena)) {
      batch_record->records.emplace_back(std::move(*record));

      if (batch_record->records.size() == batch_size_) {
        this->ff_send_out(batch_record.release());
        batch_record = std::make_unique<files::ArenaBatch>(batch_size_, arena_size);
      }
    }

    if (batch_record->records.size() > 0) {
      this->ff_send_out(batch_record.release());
    }
    return EOS;
  }

 private:
  std::filesystem::path path_;
  size_t batch_size_;
  size_t payload_length_;
};

struct BatchSorter : ff::ff_node_t<files::ArenaBatch, files::ArenaBatch> {
  auto svc(files::ArenaBatch* batch_ptr) -> files::ArenaBatch* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::unique_ptr<files::ArenaBatch>(batch_ptr);

    std::ranges::sort(batch->records, std::ranges::less());
    return batch.release();
  }
};

struct Collector : ff::ff_node_t<files::ArenaBatch> {
  auto svc(files::ArenaBatch* batch_ptr) -> files::ArenaBatch* override {
    return batch_ptr;
  }
};

struct BatchWriter : ff::ff_node_t<files::ArenaBatch, void> {
  auto svc(files::ArenaBatch* batch_ptr) -> void* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::unique_ptr<files::ArenaBatch>(batch_ptr);
    auto bytes_to_write = batch->totalBytes(header_size);

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
    // std::println("Written {} bytes to {}", bytes_to_write, temp_file.string());

    return new std::filesystem::path(std::move(temp_file));
  }
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

struct FileMerger : ff::ff_node_t<files::RecordBatch> {
  static constexpr size_t BUFFER_SIZE = 2048;

  FileMerger(const std::vector<std::filesystem::path>&& files, size_t batch_size)
      : files_(files), batch_size_(batch_size) {
  }

  auto svc(files::RecordBatch*) -> files::RecordBatch* override {
    // NOTE: Initializing loaders with large buffers takes a lot of time
    auto loaders = std::vector<std::unique_ptr<files::BufferedRecordLoader<BUFFER_SIZE>>>(
        files_.size()
    );

    std::exception_ptr first_exception = nullptr;
    std::once_flag capture_flag;  // The flag to ensure only one capture

    ff::parallel_for(
        0,
        files_.size(),
        [&](auto i) {
          try {
            loaders[i] = std::make_unique<files::BufferedRecordLoader<BUFFER_SIZE>>(files_[i]);
          } catch (std::exception& e) {
            std::call_once(capture_flag, [&]() {
              first_exception = std::current_exception();
            });
          }
        },
        16  // FF_AUTO or FF_NUM_REAL_CORES do not work (probably becuase mapping_string.sh has no
            // effect on nixos)
    );

    // Here we can safely throw the exception
    if (first_exception) {
      cleanup();
      throw first_exception;
    }

    // std::greater<> makes it a min-queue
    auto min_heap = std::
        priority_queue<files::HeapNode, std::vector<files::HeapNode>, std::greater<>>{};

    DefaultHeapAllocator<char> allocator;
    auto batch_record = std::make_unique<files::RecordBatch>(batch_size_);

    // Initial blocks read (priming the min heap)
    for (size_t i = 0; i < loaders.size(); i++) {
      auto record = loaders[i]->readNext(allocator);
      if (record) {
        min_heap.emplace(std::move(*record), i);
      }
    }
    assert(batch_size_ > 0 && batch_record->records.size() == 0);

    while (!min_heap.empty()) {
      // Since the payloads might be large to copy by value, we "cast" away the const qualifier
      // from the node's reference and move it
      auto smallest = std::move(const_cast<files::HeapNode&>(min_heap.top()));
      min_heap.pop();

      // Track batch size in bytes to avoid recomputing it later (we do it before we move the record
      // of course)
      batch_record->total_size += smallest.record.payload.size() + header_size;
      batch_record->records.emplace_back(std::move(smallest.record));

      if (batch_record->records.size() == batch_size_) {
        this->ff_send_out(batch_record.release());
        batch_record = std::make_unique<files::RecordBatch>(batch_size_);
      }

      // If we can't read the next record, then the `loader_index`-th file has been consumed
      // TODO: remove temporary file when loader is done
      if (auto next_record = loaders[smallest.loader_index]->readNext(allocator)) {
        min_heap.push(files::HeapNode{
            .record = std::move(*next_record), .loader_index = smallest.loader_index
        });
      }
    }

    if (batch_record->records.size() > 0) {
      this->ff_send_out(batch_record.release());
    }
    cleanup();
    return EOS;
  }

  auto cleanup() -> void {
    ff::parallel_for(
        0,
        files_.size(),
        [&](auto i) {
          // std::println("Deleting temporary file {}", files_[i].string());
          std::filesystem::remove(files_[i]);
        },
        16  // FF_AUTO or FF_NUM_REAL_CORES do not work (probably becuase mapping_string.sh has no
            // effect on nixos)
    );
  }

 private:
  std::vector<std::filesystem::path> files_;
  size_t batch_size_;
};

struct FileWriter : ff::ff_node_t<files::RecordBatch, void> {
  FileWriter(const FileWriter& other
  ) = delete;  // Cannot copy std::ofsteam and cannot move because of const qualifier
  FileWriter(std::filesystem::path path) : out_path_{path}, out_file_{path} {
  }

  auto svc(files::RecordBatch* batch_ptr) -> void* override {
    if (batch_ptr == nullptr) {
      return GO_ON;
    }
    auto batch = std::unique_ptr<files::RecordBatch>(batch_ptr);
    auto bytes_to_write = batch->total_size;

    if (!out_file_) {
      throw std::logic_error("Could not open output file");
    }

    auto out_buffer = std::vector<char>(bytes_to_write);
    auto out_stream = std::span<char>(out_buffer);
    for (const auto& record : batch->records) {
      files::encodeRecord(record, out_stream);
    }
    assert(out_stream.empty());
    out_file_.write(out_buffer.data(), bytes_to_write);
    // std::println("Written {} bytes to {}", bytes_to_write, out_path_.string());

    return GO_ON;
  }

 private:
  std::filesystem::path out_path_;
  std::ofstream out_file_;
};

auto printHelp(const std::string_view& prog_name) -> void {
  std::cerr
      << "Usage: " << prog_name
      << " --input <file> --output <file> --num_sorters <n> --num_writers <n> [--batch_size <batch_size>] [--verbose]"
      << std::endl;
}

auto main(int argc, char* argv[]) -> int {
  if (argc < 3) {
    printHelp(argv[0]);
    exit(1);
  }

  auto cli_parser = CliParser(argc, argv);
  constexpr size_t buffer_size = 1024UL * 1024UL;
  const auto in_path = std::filesystem::path(*cli_parser.get<std::string>("input"));
  const auto out_path = std::filesystem::path(*cli_parser.get<std::string>("output"));
  const auto num_sorters = *cli_parser.get<size_t>("num_sorters");
  const auto num_writers = *cli_parser.get<size_t>("num_writers");
  const auto batch_size = cli_parser.get<size_t>("batch_size").value_or(1'000'000);
  const auto verbose = cli_parser.get<bool>("verbose").value_or(false);
  constexpr auto write_batch_size = 1000;

  // Emitter stage: reads file and batches records
  auto sorting_pipe = ff::ff_pipeline{};
  sorting_pipe.add_stage(new Emitter<buffer_size>(in_path, batch_size, files::MINIMUM_PAYLOAD_LENGTH));

  // Sorting stage: farm of workers that sorts batches
  auto sorter_farm = ff::ff_farm();
  auto sorter_workers = std::vector<ff::ff_node*>{};
  std::generate_n(std::back_inserter(sorter_workers), num_sorters, []() {
    return new BatchSorter();
  });
  sorter_farm.add_workers(sorter_workers);
  sorter_farm.add_collector(new Collector());  // Collects sorted batches and forwards them
  sorting_pipe.add_stage(sorter_farm);

  // Writing stage: writes sorted batches into temporary files
  auto writer_farm = ff::ff_farm();
  auto writer_workers = std::vector<ff::ff_node*>{};
  std::generate_n(std::back_inserter(writer_workers), num_writers, []() {
    return new BatchWriter();
  });
  writer_farm.add_workers(writer_workers);
  sorting_pipe.add_stage(writer_farm);

  // Collection stage: collects temporary files into a vector
  auto result_collector = new ResultCollector();
  sorting_pipe.add_stage(result_collector);

  auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to sort file");
  // Wait for each batch to be written on disk
  sorting_pipe.run_and_wait_end();  // This acts as a barrier

  // Merging pipeline
  auto merging_pipe = ff::ff_pipeline{};
  merging_pipe.add_stage(new FileMerger(std::move(result_collector->sorted_files), write_batch_size));

  merging_pipe.add_stage(new FileWriter(out_path));
  merging_pipe.run_and_wait_end();

  return 0;
}
