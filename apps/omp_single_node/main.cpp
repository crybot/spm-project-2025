#include <algorithm>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "record.hpp"
#include "record_loader.hpp"
#include "thread_safe_queue.hpp"
#include "utils.hpp"

std::vector<std::filesystem::path> temp_file_paths;
std::mutex vector_mutex;  // To protect access to the shared vector of paths
constexpr auto HEADER_SIZE = sizeof(uint64_t) + sizeof(uint32_t);
constexpr auto BUFFER_SIZE = 1024UL * 1024UL;

std::atomic<size_t> total_bytes = 0;

auto sortAndWrite(std::shared_ptr<files::ArenaBatch> batch) -> void {
  if (!batch) {
    return;
  }
#pragma omp task firstprivate(batch)
  {
    std::ranges::sort(batch->records, std::ranges::less());
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
    out_file.write(out_buffer.data(), static_cast<std::streamsize>(bytes_to_write));
    // std::println("Written {} bytes to {}", bytes_to_write, temp_file.string());
    total_bytes += bytes_to_write;

    {
      std::lock_guard<std::mutex> lock(vector_mutex);
      temp_file_paths.push_back(std::move(temp_file));
    }
  }
}

auto createSortedRunsOmp(
    const std::filesystem::path& file_to_sort,
    const size_t batch_size,
    const size_t arena_size,
    const size_t payload_length = 8
) -> void {
#pragma omp parallel
  {
#pragma omp single
    {
      auto record_loader = files::
          BufferedRecordLoader<BUFFER_SIZE, MemoryArena<char>, files::RecordView>(file_to_sort);
      auto batch_record = std::make_shared<files::ArenaBatch>(batch_size, arena_size);

      while (auto record = record_loader.readNext(batch_record->arena)) {
        batch_record->records.emplace_back(std::move(*record));

        if (batch_record->records.size() == batch_size) {
          sortAndWrite(std::move(batch_record));
          batch_record = std::make_shared<files::ArenaBatch>(batch_size, arena_size);
        }
      }

      if (batch_record->records.size() > 0) {
        sortAndWrite(std::move(batch_record));
      }
#pragma omp taskwait
    }
  }
}

auto cleanupTemporaryFiles(const std::vector<std::filesystem::path>& file_paths) -> void {
#pragma omp parallel for
  for (auto i = 0UL; i < file_paths.size(); i++) {
    // std::println("Deleting temporary file {}", file_paths[i].string());
    std::filesystem::remove(file_paths[i]);
  }
}

auto writeOutputFile(std::ofstream& out_file, std::shared_ptr<files::RecordBatch> batch) -> void {
  if (!batch) {
    return;
  }
  if (!out_file) {
    throw std::logic_error("Could not open output file");
  }

  auto bytes_to_write = batch->total_size;
  auto out_buffer = std::vector<char>(bytes_to_write);
  auto out_stream = std::span<char>(out_buffer);

  for (const auto& record : batch->records) {
    files::encodeRecord(record, out_stream);
  }
  assert(out_stream.empty());

  out_file.write(out_buffer.data(), static_cast<std::streamsize>(bytes_to_write));
  // std::println("Written {} bytes to output file", bytes_to_write);
}

auto mergeFiles(const std::vector<std::filesystem::path>& files, size_t batch_size) -> void {
  using files::HeapNode;
  std::exception_ptr first_exception = nullptr;
  std::once_flag capture_flag;  // The flag to ensure only one capture
  auto loaders = std::vector<std::unique_ptr<files::BufferedRecordLoader<2048>>>(files.size());

#pragma omp parallel for
  for (auto i = 0UL; i < loaders.size(); i++) {
    try {
      loaders[i] = std::make_unique<files::BufferedRecordLoader<2048>>(files[i]);
    } catch (std::exception& e) {
      std::call_once(capture_flag, [&]() {
        first_exception = std::current_exception();
      });
    }
  }

  // Here we can safely throw the exception
  if (first_exception) {
    cleanupTemporaryFiles(files);
    throw first_exception;
  }

  auto task_queue = ThreadSafeQueue<std::shared_ptr<files::RecordBatch>>{};
  auto out_file = std::ofstream(std::filesystem::path("sorted.bin"));

#pragma omp parallel
  {
#pragma omp single  // Producer task, executed by a single thread
    {
#pragma omp task shared(out_file, task_queue)  // Consumer task
      {
        while (true) {
          auto batch_to_write = task_queue.pop();
          if (batch_to_write == nullptr)
            break;
          writeOutputFile(out_file, std::move(batch_to_write));
        }
      }

      // std::greater<> makes it a min-queue
      auto min_heap = std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<>>{};

      DefaultHeapAllocator<char> allocator;
      auto batch_record = std::make_shared<files::RecordBatch>(batch_size);

      // Initial blocks read (priming the min heap)
      // NOTE: we do this sequentially because we just need to read once record per loader and
      // synchronizing the min-heap would mostly add overhead
      for (size_t i = 0; i < loaders.size(); i++) {
        auto record = loaders[i]->readNext(allocator);
        if (record) {
          min_heap.emplace(std::move(*record), i);
        }
      }
      assert(batch_size > 0 && batch_record->records.size() == 0);

      while (!min_heap.empty()) {
        // Since the payloads might be large to copy by value, we "cast" away the const qualifier
        // from the node's reference and move it
        auto smallest = std::move(const_cast<files::HeapNode&>(min_heap.top()));
        min_heap.pop();

        // Track batch size in bytes to avoid recomputing it later (we do it before we move the
        // record of course)
        batch_record->total_size += smallest.record.payload.size() + HEADER_SIZE;
        batch_record->records.emplace_back(std::move(smallest.record));

        if (batch_record->records.size() == batch_size) {
          task_queue.push(std::move(batch_record));
          batch_record = std::make_shared<files::RecordBatch>(batch_size);
        }

        // If we can't read the next record, then the `loader_index`-th file has been consumed
        if (auto next_record = loaders[smallest.loader_index]->readNext(allocator)) {
          min_heap.push(files::HeapNode{
              .record = std::move(*next_record), .loader_index = smallest.loader_index
          });
        }
      }

      if (batch_record->records.size() > 0) {
        task_queue.push(std::move(batch_record));
      }

      task_queue.push(nullptr);  // Signal end of stream

      cleanupTemporaryFiles(files);

#pragma omp taskwait  // Wait for pending tasks to finish
    }  // single
  }  // parallel
}

auto main(int, char* argv[]) -> int {
  auto path = std::filesystem::path(argv[1]);
  const size_t batch_size = std::stoi(argv[2]);
  const size_t arena_size = batch_size * files::MINIMUM_PAYLOAD_LENGTH;

  createSortedRunsOmp(path, batch_size, arena_size);
  // std::println("Total bytes written: {}", total_bytes.load());

  mergeFiles(temp_file_paths, 1000);

  return 0;
}
