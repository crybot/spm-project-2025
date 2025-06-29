
#include <filesystem>
#include <string>

#include "cli_parser.hpp"
#include "parallel_sorter.hpp"
#include "stop_watch.hpp"

auto printHelp(const std::string_view& prog_name) -> void {
  std::cerr << "Usage: " << prog_name
            << " --input <file> [--batch_size <batch_size>]"
            << std::endl;
}

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    printHelp(argv[0]);
    exit(1);
  }

  auto cli_parser = CliParser(argc, argv);
  constexpr size_t buffer_size = 1024UL * 1024UL;
  const auto batch_size = cli_parser.get<size_t>("batch_size").value_or(1'000'000);
  const auto in_path = std::filesystem::path(*cli_parser.get<std::string>("input"));

  auto total_watch = StopWatch<std::chrono::milliseconds>("Time to terminate");

  auto record_loader = files::
      BufferedRecordLoader<buffer_size, MemoryArena<char>, files::RecordView>(in_path);
  auto record_batch = std::make_unique<files::ArenaBatch>(
      batch_size, batch_size * files::MINIMUM_PAYLOAD_LENGTH
  );

  {
    auto read_watch = StopWatch<std::chrono::milliseconds>("Time to read input file in memory");
    while (auto record = record_loader.readNext(record_batch->arena)) {
      record_batch->records.emplace_back(std::move(*record));
    }
  }

  {
    auto sort_watch = StopWatch<std::chrono::milliseconds>("Time to sort input file in memory");
    std::ranges::sort(record_batch->records, std::ranges::less());
  }

  return 0;
}
