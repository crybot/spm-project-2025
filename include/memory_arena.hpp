#pragma once
#include <cassert>
#include <memory>
#include <span>
#include <vector>

// Implement a very simple block-contiguous memory arena.
// The arena is initialized with a single block of `size` elements of type T. Subsequent allocations with the `alloc`
// member function return a contiguous view if the requested size fits within the current block; if not, a new block of
// approriate size is heap-allocated (the default size is doubled until the number of requested elements fits). Previous
// elements within older blocks are discarded, leading to high fragmentation and wasted resources if the initial block
// size is not much bigger than the requested allocations.
template <typename T>
class MemoryArena {
 public:
  MemoryArena(size_t);
  MemoryArena(MemoryArena &) = delete;
  MemoryArena(MemoryArena &&) = delete;
  auto operator=(const MemoryArena &) -> MemoryArena & = delete;
  auto operator=(MemoryArena &&) -> MemoryArena & = delete;

  // ~MemoryArena(); // default constructor is fine

  auto alloc(size_t) -> std::span<T>;
  auto used() -> size_t;

 private:
  std::vector<std::unique_ptr<T>> blocks_{};
  size_t block_size_;
  size_t begin_{0};
  size_t end_;
  size_t used_{0};

  auto extend() -> void;
};

template <typename T>
MemoryArena<T>::MemoryArena(size_t size) : block_size_{size}, end_{size} {
  blocks_.emplace_back(new T[block_size_]);
}

template <typename T>
auto MemoryArena<T>::extend() -> void {
  blocks_.emplace_back(new T[block_size_]);
  begin_ = 0;
  end_ = block_size_;
}

template <typename T>
auto MemoryArena<T>::alloc(size_t size) -> std::span<T> {
  if (size > end_ - begin_) {
    while (size > block_size_) {
      block_size_ *= 2;
    }
    extend();
  }

  auto view = std::span<T>(blocks_.back().get(), block_size_).subspan(begin_, size);

  begin_ = begin_ + size;
  used_ = used_ + size;
  return view;
}

template <typename T>
auto MemoryArena<T>::used() -> size_t {
  return used_ * sizeof(T);
}

template <typename T>
class DefaultHeapAllocator {
 public:
  auto alloc(size_t size) -> std::vector<T> {
    return std::vector<T>(size);
  }
};
