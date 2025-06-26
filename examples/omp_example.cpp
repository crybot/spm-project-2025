#include <iostream>

auto main() -> int {
  #pragma omp parallel
  std::cout << "Hello world!" << std::endl;
  return 0;
}
