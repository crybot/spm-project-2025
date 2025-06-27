#pragma once
#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>
#include <algorithm>

#include "record.hpp"
#include "record_loader.hpp"
#include "thread_safe_queue.hpp"
#include "utils.hpp"

class AbstractParallelSorter {
 public:
  AbstractParallelSorter(size_t reading_batch_size, size_t writing_batch_size)
      : reading_batch_size_(reading_batch_size),
        writing_batch_size_(writing_batch_size),
        arena_size_(reading_batch_size_ * files::MINIMUM_PAYLOAD_LENGTH) {};

  auto sort(const std::filesystem::path& input_path, const std::filesystem::path& output_path)
      -> void {
    createSortedRuns(input_path);
    mergeSortedRuns(output_path);
  }

  auto virtual createSortedRuns(const std::filesystem::path& input_path) -> void = 0;
  auto virtual mergeSortedRuns(const std::filesystem::path& output_path) -> void = 0;

 protected:
  size_t reading_batch_size_;
  size_t writing_batch_size_;
  size_t arena_size_;
};

template <size_t BUFFER_SIZE>
class ParallelSorterOMP : public AbstractParallelSorter {
 public:
  ParallelSorterOMP(size_t reading_batch_size, size_t writing_batch_size, bool verbose = false)
      : AbstractParallelSorter(reading_batch_size, writing_batch_size), verbose_(verbose) {
  }
  virtual ~ParallelSorterOMP() = default;
  auto createSortedRuns(const std::filesystem::path& input_path) -> void override;
  auto mergeSortedRuns(const std::filesystem::path& output_path) -> void override;

 private:
  auto cleanupTemporaryFiles() -> void;
  auto sortAndWrite(std::shared_ptr<files::ArenaBatch>) -> void;
  auto writeOutputBatch(std::shared_ptr<files::RecordBatch>, std::ofstream&) -> void;

  constexpr static size_t merge_buffer_size_ = 2028;
  std::vector<std::filesystem::path> temp_file_paths_;
  std::mutex vector_mutex_;  // To protect access to the shared vector of paths
  bool verbose_;
};

template <size_t BUFFER_SIZE>
auto ParallelSorterOMP<BUFFER_SIZE>::createSortedRuns(const std::filesystem::path& input_path)
    -> void {
#pragma omp parallel
  {
#pragma omp single
    {
      auto record_loader = files::
          BufferedRecordLoader<BUFFER_SIZE, MemoryArena<char>, files::RecordView>(input_path);
      auto batch_record = std::make_shared<files::ArenaBatch>(
          this->reading_batch_size_, this->arena_size_
      );

      while (auto record = record_loader.readNext(batch_record->arena)) {
        batch_record->records.emplace_back(std::move(*record));

        if (batch_record->records.size() == this->reading_batch_size_) {
          sortAndWrite(std::move(batch_record));
          batch_record = std::make_shared<files::ArenaBatch>(
              this->reading_batch_size_, this->arena_size_
          );
        }
      }

      if (batch_record->records.size() > 0) {
        sortAndWrite(std::move(batch_record));
      }
#pragma omp taskwait
    }
  }
}

template <size_t BufferSize>
auto ParallelSorterOMP<BufferSize>::mergeSortedRuns(const std::filesystem::path& output_path)
    -> void {
  using files::HeapNode;
  std::exception_ptr first_exception = nullptr;
  std::once_flag capture_flag;  // The flag to ensure only one capture
  auto loaders = std::vector<std::unique_ptr<files::BufferedRecordLoader<merge_buffer_size_>>>(
      temp_file_paths_.size()
  );

#pragma omp parallel for
  for (auto i = 0UL; i < loaders.size(); i++) {
    try {
      loaders[i] = std::make_unique<files::BufferedRecordLoader<merge_buffer_size_>>(
          temp_file_paths_[i]
      );
    } catch (std::exception& e) {
      std::call_once(capture_flag, [&]() {
        first_exception = std::current_exception();
      });
    }
  }

  // Here we can safely throw the exception
  if (first_exception) {
    cleanupTemporaryFiles();
    throw first_exception;
  }

  auto task_queue = ThreadSafeQueue<std::shared_ptr<files::RecordBatch>>{};
  auto out_file = std::ofstream(output_path);

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
          writeOutputBatch(std::move(batch_to_write), out_file);
        }
      }

      // std::greater<> makes it a min-queue
      auto min_heap = std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<>>{};

      DefaultHeapAllocator<char> allocator;
      auto batch_record = std::make_shared<files::RecordBatch>(writing_batch_size_);

      // Initial blocks read (priming the min heap)
      // NOTE: we do this sequentially because we just need to read once record per loader and
      // synchronizing the min-heap would mostly add overhead
      for (size_t i = 0; i < loaders.size(); i++) {
        auto record = loaders[i]->readNext(allocator);
        if (record) {
          min_heap.emplace(std::move(*record), i);
        }
      }
      assert(writing_batch_size_ > 0 && batch_record->records.size() == 0);

      while (!min_heap.empty()) {
        // Since the payloads might be large to copy by value, we "cast" away the const qualifier
        // from the node's reference and move it
        auto smallest = std::move(const_cast<files::HeapNode&>(min_heap.top()));
        min_heap.pop();

        // Track batch size in bytes to avoid recomputing it later (we do it before we move the
        // record of course)
        batch_record->total_size += smallest.record.payload.size() + files::HEADER_SIZE;
        batch_record->records.emplace_back(std::move(smallest.record));

        if (batch_record->records.size() == writing_batch_size_) {
          task_queue.push(std::move(batch_record));
          batch_record = std::make_shared<files::RecordBatch>(writing_batch_size_);
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

      cleanupTemporaryFiles();

#pragma omp taskwait  // Wait for pending tasks to finish
    }  // single
  }  // parallel
}

template <size_t BufferSize>
auto ParallelSorterOMP<BufferSize>::writeOutputBatch(
    std::shared_ptr<files::RecordBatch> batch, std::ofstream& output_file
) -> void {
  if (!batch) {
    return;
  }
  if (!output_file) {
    throw std::logic_error("Could not open output file");
  }

  auto bytes_to_write = batch->total_size;
  auto out_buffer = std::vector<char>(bytes_to_write);
  auto out_stream = std::span<char>(out_buffer);

  for (const auto& record : batch->records) {
    files::encodeRecord(record, out_stream);
  }
  assert(out_stream.empty());

  output_file.write(out_buffer.data(), static_cast<std::streamsize>(bytes_to_write));
  if (verbose_) {
    std::cout << std::format("Written {} bytes to output file\n", bytes_to_write);
  }
}

template <size_t BufferSize>
auto ParallelSorterOMP<BufferSize>::sortAndWrite(std::shared_ptr<files::ArenaBatch> batch) -> void {
  if (!batch) {
    return;
  }
#pragma omp task firstprivate(batch)
  {
    std::ranges::sort(batch->records, std::ranges::less());
    auto bytes_to_write = batch->totalBytes(files::HEADER_SIZE);
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
    if (verbose_) {
      std::cout << std::format("Written {} bytes to {}\n", bytes_to_write, temp_file.string());
    }

    {
      std::lock_guard<std::mutex> lock(vector_mutex_);
      temp_file_paths_.push_back(std::move(temp_file));
    }
  }
}

template <size_t BufferSize>
auto ParallelSorterOMP<BufferSize>::cleanupTemporaryFiles() -> void {
#pragma omp parallel for
  for (auto i = 0UL; i < temp_file_paths_.size(); i++) {
    if (verbose_) {
      std::cout << std::format("Deleting temporary file {}\n", temp_file_paths_[i].string());
    }
    std::filesystem::remove(temp_file_paths_[i]);
  }
}

// class ParallelSorterFF : public AbstractParallelSorter {};
