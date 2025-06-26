#include <ff/ff.hpp>
//
#include <mpi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <ff/parallel_for.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <utility>

#include "memory_arena.hpp"
#include "record.hpp"
#include "record_loader.hpp"
#include "stop_watch.hpp"
#include "utils.hpp"

/*
 *                            SPM PROJECT (multi-node: MPI + FF)
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
 * - We can assume 32GB of ram per node
 *
 * File system:
 * - home directory exported via NFS: input/output files are accessible by each node
 * - 10Gb ethernet
 *
 * Implementation:
 * - Use MPI_Init_thread instead of MPI_Init (because we use threads internally with omp or ff)
 * - Emitter:
 *    * Read large batches from the input file;
 *    * For each batch scatter it to worker nodes;
 *    * When EOF reached, send an MPI_REQUEST_NULL to all worker nodes;
 * - Worker:
 *    * Sort each batch in memory and store it on a local file (maybe /tmp can be used as scratch)
 *    * When MPI_REQUEST_NULL is received, apply a K-way merge
 *    * Couple options here: either send the locally sorted file through the network with MPI (they
 * might be huge) or write the file to the NFS mounted directory to make it accessible to the
 * collector.
 *    * Synchronize the Collector by signaling that the work is done.
 *
 * - Collector:
 *    * Wait for each worker to finish
 *    * Apply an external memory merge to the sorted runs (either stored on the NFS mounted
 * directory, or received through MPI)
 *    * Write the file to disk
 */

constexpr int EOS_SIGNAL = -1;
constexpr int ROOT_RANK = 0;

struct SerializedBatch {
  std::vector<char> bytes;
  std::vector<size_t> offsets;
};

// NOTE: std::ispanstream is not be supported by GCC 12.2.0. We thus provide an equivalent
// alternative MemoryViewInputStream that holds a non-owining view to a contiguous region of memory
auto deserializeBatch(std::vector<char>& batch) -> std::vector<files::RecordView> {
  // auto byte_stream = std::ispanstream(batch);
  auto byte_stream = files::MemoryViewInputStream{batch};
  auto record_loader = files::BufferedRecordLoader<1024 * 1024>(byte_stream);
  auto allocator = DefaultHeapAllocator<char>{};

  while (auto record = record_loader.readNext(allocator)) {
    std::cout << *record << std::endl;
  }

  return {};  // TODO;
}

auto serializeBatch(std::shared_ptr<files::ArenaBatch> batch) -> SerializedBatch {
  auto serialized = std::vector<char>(batch->totalBytes());
  auto offsets = std::vector<size_t>(batch->records.size());
  auto byte_stream = std::span(serialized);

  size_t offset = 0;
  size_t i = 0;
  for (auto& record : batch->records) {
    offsets[i++] = offset;
    offset += files::encodeRecord(record, byte_stream);
  }
  assert(!byte_stream.size());
  return {.bytes = std::move(serialized), .offsets = std::move(offsets)};
}

// TODO: defined custom Emitter-to-Workers communicator (leave out collector)
auto scatterBatch(std::shared_ptr<files::ArenaBatch> batch, int world_size, MPI_Comm communicator) -> void {
  const auto root_rank = 0;
  const auto num_workers = world_size - 1;

  auto serialized_batch = serializeBatch(batch);
  const auto& offsets = serialized_batch.offsets;
  const auto records_per_worker = offsets.size() / num_workers;  // Last worker gets all remaining
  const auto total_bytes = static_cast<int>(serialized_batch.bytes.size());

  assert(world_size >= 3);  // at least 2 workers + emitter

  std::cout << std::format("Num records: {}", batch->records.size()) << std::endl;
  std::cout << std::format("Sending {}/{} records per worker", records_per_worker, offsets.size())
            << std::endl;

  // Prepare send counts (ignore ranks 0; rank world_size - 1 is treated right
  // after the loop) Basically computes adjacent differences with a stride of `records_per_worker`
  auto send_counts = std::vector<int>(world_size, 0);  // Initialize to 0
  auto last_count = 0;
  for (auto w = 1; w < world_size - 1; w++) {
    send_counts[w] = static_cast<int>(offsets[records_per_worker * w]) - last_count;
    last_count = send_counts[w];
  }
  send_counts[world_size - 1] = total_bytes -
                                static_cast<int>(offsets[records_per_worker * (world_size - 2)]);

  // MPI_IN_PLACE avoids the copy from send_counts[0] into a local buffer in the root node
  std::cout << "Sending counts ";
  std::ranges::copy(send_counts, std::ostream_iterator<int>(std::cout, " "));
  std::cout << std::endl;
  MPI_Scatter(send_counts.data(), 1, MPI_INT, MPI_IN_PLACE, 1, MPI_INT, root_rank, communicator);
}

auto signalEos(int world_size, MPI_Comm communicator) -> void {
  auto v = std::vector<int>(world_size, EOS_SIGNAL);
  MPI_Scatter(v.data(), 1, MPI_INT, MPI_IN_PLACE, 1, MPI_INT, ROOT_RANK, communicator);
};

template <size_t BUFFER_SIZE>
auto createBatches(const std::filesystem::path& file_to_sort, const size_t batch_size, MPI_Comm communicator) -> void {
  constexpr size_t arena_size = BUFFER_SIZE * 8;
  auto record_loader = files::
      BufferedRecordLoader<BUFFER_SIZE, MemoryArena<char>, files::RecordView>(file_to_sort);
  auto batch_record = std::make_shared<files::ArenaBatch>(batch_size, arena_size);

  int world_size{0};
  MPI_Comm_size(communicator, &world_size);

#pragma omp parallel
  {
#pragma omp single
    {
      auto batch_stop_watch = StopWatch<std::chrono::microseconds>("Time to read batch");
      while (auto record = record_loader.readNext(batch_record->arena)) {
        batch_record->records.emplace_back(std::move(*record));

        if (batch_record->records.size() == batch_size) {
          batch_stop_watch.reset();
#pragma omp task firstprivate(batch_record) depend(inout : batch_record)
          {
            auto stop_watch = StopWatch<std::chrono::microseconds>("Time to serialize batch");
            scatterBatch(std::move(batch_record), world_size, communicator);
          }
          batch_record = std::make_shared<files::ArenaBatch>(batch_size, arena_size);
        }
      }

      if (batch_record->records.size() > 0) {
#pragma omp task firstprivate(batch_record) depend(in : batch_record)
        {
          auto stop_watch = StopWatch<std::chrono::microseconds>("Time to serialize batch");
          scatterBatch(std::move(batch_record), world_size, communicator);
        }
      }
#pragma omp taskwait
    }  // single
  }  // parallel
  signalEos(world_size, communicator);
}

auto emitter(const std::filesystem::path& file_to_sort, size_t batch_size, MPI_Comm communicator) -> void {
  std::cout << "Initializing emitter..." << std::endl;
  createBatches<1024 * 1024>(file_to_sort, batch_size, communicator);
  std::cout << "Emitter's work done" << std::endl;
}

auto worker(int rank, MPI_Comm communicator) -> void {
  constexpr auto root_rank = 0;
  std::cout << "Initializing worker..." << std::endl;
  int count{0};

  while (true) {
    MPI_Scatter(nullptr, 0, MPI_INT, &count, 1, MPI_INT, root_rank, communicator);
    std::cout << std::format("Worker {}: Received count = {}", rank, count) << std::endl;
    if (count == EOS_SIGNAL) {
      break;
    }
  }

  std::cout << std::format("Worker {} terminating", rank) << std::endl;
}

auto collector() -> void {
  std::cout << "Initializing collector..." << std::endl;
}

auto main(int argc, char* argv[]) -> int {
  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

  int rank;
  int world_size;

  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  const auto emitter_rank = 0;
  const auto collector_rank = world_size - 1;
  const auto color = (rank == collector_rank) ? 1 : 0;
  MPI_Comm emitter_workers_comm;

  MPI_Comm_split(MPI_COMM_WORLD, color, rank, &emitter_workers_comm);

  if (argc < 2) {
    if (rank == 0) {
      std::cerr << "Usage: <command> <file>" << std::endl;
    }
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  constexpr auto batch_size = 1'000'000;
  const auto path = std::filesystem::path(argv[1]);

  if (rank == 0) {  // emitter
    emitter(path, batch_size, emitter_workers_comm);
  }
  else if (rank == world_size - 1) {  // collector
    collector();
  }
  else {  // worker
    worker(rank, emitter_workers_comm);
  }

  MPI_Finalize();
  return 0;
}
