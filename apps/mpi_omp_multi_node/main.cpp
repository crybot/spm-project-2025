#include <mpi.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <ranges>

#include "memory_arena.hpp"
#include "parallel_sorter.hpp"
#include "record.hpp"
#include "record_loader.hpp"
#include "stop_watch.hpp"
#include "cli_parser.hpp"

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
 *    * When EOF reached, notify it all worker nodes;
 * - Worker:
 *    * Sort each batch in memory and store it on a local file (maybe /tmp can be used as scratch)
 *    * When EOF message is received, apply a K-way merge
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

auto nameSortedFile(int rank) -> std::filesystem::path {
  return format("sorted-", rank, ".bin");
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

template <size_t BufferSize>
auto deserializeBatch(std::vector<char>& batch) -> std::shared_ptr<files::ArenaBatch> {
  // NOTE: std::ispanstream is not be supported by GCC 12.2.0. We thus provide an equivalent
  // alternative MemoryViewInputStream that holds a non-owining view to a contiguous region of
  // memory
  auto byte_stream = files::MemoryViewInputStream{batch};
  auto record_loader = files::
      BufferedRecordLoader<BufferSize, MemoryArena<char>, files::RecordView>(byte_stream);
  auto record_batch = std::make_shared<files::ArenaBatch>(batch.size(), batch.size());

  while (auto record = record_loader.readNext(record_batch->arena)) {
    record_batch->records.emplace_back(std::move(*record));
  }

  return record_batch;
}

auto scatterBatch(std::shared_ptr<files::ArenaBatch> batch, int world_size, MPI_Comm communicator, bool verbose = false)
    -> void {
  const auto num_workers = world_size - 1;

  // Serialize batch of records into a contiguous byte-stream
  auto serialized_batch = serializeBatch(batch);
  const auto& offsets = serialized_batch.offsets;
  const auto records_per_worker = offsets.size() / num_workers;  // Last worker gets all remaining bytes
  const auto total_bytes = static_cast<int>(serialized_batch.bytes.size());

  assert(world_size >= 2);  // at least 1 worker + emitter

  // Prepare send counts and displacements
  // Ignore ranks 0;
  // Last worker takes any extra bytes
  // TODO: check offsets and displacements overflow overflows (batch_size <
  // std::numeric_limits<int>)
  auto send_counts = std::vector<int>(world_size, 0);
  auto displacements = std::vector<int>(world_size, 0);
  size_t last_end = 0;
  size_t computed_total = 0;  // Used as a sanity check

  for (auto w = 1; w < world_size; w++) {
    auto start_offset = last_end;
    auto end_offset = (w == world_size - 1) ? total_bytes : offsets[records_per_worker * w];

    send_counts[w] = static_cast<int>(end_offset - start_offset);
    displacements[w] = static_cast<int>(start_offset);
    last_end = end_offset;
    computed_total += send_counts[w];
    assert(displacements[w] < static_cast<int>(serialized_batch.bytes.size()));
  }
  assert(computed_total == serialized_batch.bytes.size());

  if (verbose) {
    std::cout << "Sending counts" << std::endl;
  }
  // Send counts
  // MPI_IN_PLACE avoids the copy from send_counts[0] into a local buffer in the root node
  MPI_Scatter(send_counts.data(), 1, MPI_INT, MPI_IN_PLACE, 1, MPI_INT, ROOT_RANK, communicator);

  if (verbose) {
    std::cout << "Sending serialized batch" << std::endl;
  }
  // Send variable-sized records
  MPI_Scatterv(
      serialized_batch.bytes.data(),
      send_counts.data(),
      displacements.data(),
      MPI_BYTE,
      MPI_IN_PLACE,
      0,
      MPI_BYTE,
      ROOT_RANK,
      communicator
  );
}

auto signalEos(int world_size, MPI_Comm communicator) -> void {
  auto v = std::vector<int>(world_size, EOS_SIGNAL);
  MPI_Scatter(v.data(), 1, MPI_INT, MPI_IN_PLACE, 1, MPI_INT, ROOT_RANK, communicator);
};

template <size_t BufferSize>
auto createBatches(
    const std::filesystem::path& file_to_sort, const size_t batch_size, MPI_Comm communicator, bool verbose = false
) -> void {
  int world_size{0};
  MPI_Comm_size(communicator, &world_size);

  auto parallel_sorter = ParallelSorterOMP<BufferSize>(batch_size, 0, true);

  parallel_sorter.collectBatches(file_to_sort, [&](auto batch_record) {
    scatterBatch(std::move(batch_record), world_size, communicator, verbose);
  });

  signalEos(world_size, communicator);
}

// NOTE: The emitter rank also acts as a collector
template <size_t BufferSize>
auto emitter(
    const std::filesystem::path& file_to_sort,
    const std::filesystem::path& out_path,
    size_t batch_size,
    MPI_Comm communicator,
    bool verbose = false
) -> void {
  if (verbose) {
    std::cout << "Initializing emitter..." << std::endl;
  }
  createBatches<BufferSize>(file_to_sort, batch_size, communicator, verbose);

  int world_size{0};
  MPI_Comm_size(communicator, &world_size);

  // Vector of sorted file paths computed by worker nodes (fixed naming scheme)
  auto sorted_file_paths = [=]() {
    auto v = std::vector<std::filesystem::path>{};
    for (auto rank : std::views::iota(1, world_size)) {
      v.emplace_back(nameSortedFile(rank));
    }
    return v;
  }();

  if (verbose) {
    std::cout << "Waiting for all workers to terminate" << std::endl;
  }
  MPI_Barrier(communicator);

  const auto write_batch_size = 1000;
  auto parallel_sorter = ParallelSorterOMP<BufferSize>(0, write_batch_size, verbose);
  parallel_sorter.setFilePaths(std::move(sorted_file_paths));
  parallel_sorter.mergeSortedRuns(out_path);

  if (verbose) {
    std::cout << "Emitter's work done" << std::endl;
  }
}

template <size_t BufferSize>
auto worker(int rank, MPI_Comm communicator, bool verbose = false) -> void {
  if (verbose) {
    std::cout << "Initializing worker..." << std::endl;
  }

  const auto write_batch_size = 1000;
  const auto out_path = nameSortedFile(rank);
  auto parallel_sorter = ParallelSorterOMP<BufferSize>(0, write_batch_size, verbose);

  int count{0};
#pragma omp parallel
#pragma omp single
  {
    while (true) {
      MPI_Scatter(nullptr, 0, MPI_INT, &count, 1, MPI_INT, ROOT_RANK, communicator);

      if (verbose) {
        std::cout << "Worker " << rank << ": Received count = " << count << std::endl;
      }
      if (count == EOS_SIGNAL) {
        break;
      }

      auto serialized_records = std::vector<char>(count);
      MPI_Scatterv(
          nullptr,
          nullptr,
          nullptr,
          MPI_BYTE,
          serialized_records.data(),
          count,
          MPI_BYTE,
          ROOT_RANK,
          communicator
      );
      auto records = deserializeBatch<BufferSize>(serialized_records);

#pragma omp task firstprivate(records) depend(inout : count)
      parallel_sorter.sortAndWrite(records);
    }
#pragma omp taskwait
  }  // single

  parallel_sorter.mergeSortedRuns(out_path);

  if (verbose) {
    std::cout << "Worker " << rank << " terminating" << std::endl;
  }
  MPI_Barrier(communicator);
}

auto printHelp(const std::string_view& prog_name) -> void {
  std::cerr << "Usage: " << prog_name << " --input <file> --output <file> [--batch_size <batch_size>] [--verbose]"
            << std::endl;
}

auto main(int argc, char* argv[]) -> int {
  int provided;
  MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &provided);

  int rank;
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm emitter_workers_comm = MPI_COMM_WORLD;

  if (argc < 3) {
    if (rank == 0) {
      printHelp(argv[0]);
    }
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  auto cli_parser = CliParser(argc, argv);
  constexpr size_t buffer_size = 1024UL * 1024UL;
  const auto in_path = std::filesystem::path(*cli_parser.get<std::string>("input"));
  const auto out_path = std::filesystem::path(*cli_parser.get<std::string>("output"));
  const auto batch_size = cli_parser.get<size_t>("batch_size").value_or(1'000'000);
  const auto verbose = cli_parser.get<bool>("verbose").value_or(false);

  if (rank == 0) {  // emitter
    auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to sort file");
    emitter<buffer_size>(in_path, out_path, batch_size, emitter_workers_comm, verbose);
  }
  else {  // worker
    worker<buffer_size>(rank, emitter_workers_comm, verbose);
  }

  MPI_Finalize();
  return 0;
}
