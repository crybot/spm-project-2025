#include "memory_arena.hpp"
// #include <print>

auto main() -> int {
  MemoryArena<int>* arena = nullptr;

  {
    arena = new MemoryArena<int>(128);
  }

  auto v1 = arena->alloc(16);
  v1[0] = 1;
  v1[15] = 15;
  for (auto e : v1) {
    // std::print("{} ", e);
  }
  // std::println();

  auto v2 = arena->alloc(16);
  v2[0] = 2;
  v2[15] = 16;
  for (auto e : v2) {
    // std::print("{} ", e);
  }
  // std::println();

  auto v3 = arena->alloc(100);
  v3[0] = 3;
  v3[15] = 17;
  for (auto e : v3) {
    // std::print("{} ", e);
  }
  // std::println();

  for (auto e : v1) {
    // std::print("{} ", e);
  }
  // std::println();

  for (auto e : v2) {
    // std::print("{} ", e);
  }
  // std::println();

  for (auto e : v3) {
    // std::print("{} ", e);
  }
  // std::println();

  return 0;
}
