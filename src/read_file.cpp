#include <iostream>
#include <print>

#include "utils.hpp"

auto main(int, char* argv[]) -> int {
  auto path = std::filesystem::path(argv[1]);
  std::cout << files::readFile(path) << std::endl;
  return 0;
}
