#include <print>

auto main() -> int {
  #pragma omp parallel
  std::println("Hello world!");
  return 0;
}
