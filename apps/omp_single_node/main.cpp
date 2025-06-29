#include <filesystem>
#include <string>

#include "parallel_sorter.hpp"
#include "cli_parser.hpp"
#include "stop_watch.hpp"

auto printHelp(const std::string_view& prog_name) -> void {
  std::cerr << "Usage: " << prog_name << " --input <file> --output <file> [--batch_size <batch_size>] [--verbose]"
            << std::endl;
}

auto main(int argc, char* argv[]) -> int {
  if (argc < 3) {
    printHelp(argv[0]);
    exit(1);
  }

  auto cli_parser = CliParser(argc, argv);
  constexpr size_t buffer_size = 1024UL * 1024UL;
  const auto batch_size = cli_parser.get<size_t>("batch_size").value_or(1'000'000);
  const auto in_path = std::filesystem::path(*cli_parser.get<std::string>("input"));
  const auto out_path = std::filesystem::path(*cli_parser.get<std::string>("output"));
  const auto verbose = cli_parser.get<bool>("verbose").value_or(false);
  constexpr auto write_batch_size = 1000;

  auto stop_watch = StopWatch<std::chrono::milliseconds>("Time to sort file");
  auto parallel_sorter = ParallelSorterOMP<buffer_size>(batch_size, write_batch_size, verbose);
  parallel_sorter.sort(in_path, out_path);

  return 0;
}
