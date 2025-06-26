#include "utils.hpp"
#include <iostream>

auto main(int, char* argv[]) -> int {
  const int num_records = std::stoi(argv[1]);
  const uint32_t max_payload_length = std::stoi(argv[2]);
  constexpr uint32_t seed = 42;
  auto path = std::filesystem::path(argv[3]);
  files::generateRandomFile(path, num_records, max_payload_length, seed);

  std::cout << "File created succesfully at " << std::filesystem::current_path().string() << "/"
            << path.string() << std::endl;
  return 0;
}
