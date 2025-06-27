#include <filesystem>
#include <string>

#include "parallel_sorter.hpp"

auto main(int, char* argv[]) -> int {
  auto path = std::filesystem::path(argv[1]);
  const size_t batch_size = std::stoi(argv[2]);
  constexpr auto buffer_size = 1024UL * 1024UL;
  constexpr auto write_batch_size = 1000;
  constexpr bool verbose = true;

  auto parallel_sorter = ParallelSorterOMP<buffer_size>(batch_size, write_batch_size, verbose);
  parallel_sorter.sort(path, "sorted.bin");

  return 0;
}
