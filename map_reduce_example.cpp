#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ff/ff.hpp>
#include <iterator>
#include <print>
#include <random>
#include <thread>

using namespace std::chrono_literals;
const auto MAX_VALUE = 10;

// Not really needed to be atomic since the collector is single-threaded
std::atomic<int64_t> counter = 0;

struct IntegerEmitter : ff::ff_node_t<int> {
  IntegerEmitter(int iterations)
      : ITERATIONS(iterations), rnd_device_(), gen_(rnd_device_()), distrib_(1, MAX_VALUE) {
  }

  auto svc_init() -> int override {
    return 0;
  }

  auto svc(int *data) -> int * override {
    for (auto i = 0; i < ITERATIONS; i++) {
      auto rnd = std::make_unique<int>(distrib_(gen_));
      ff_send_out(rnd.release());
    }

    return EOS;
  }

 private:
  const int ITERATIONS;
  std::random_device rnd_device_;
  std::mt19937 gen_;
  std::uniform_int_distribution<> distrib_;
};

struct SquareWorker : ff::ff_node_t<int> {
  SquareWorker() noexcept = default;

  auto svc(int *data) -> int * override {
    if (data != nullptr) {
      std::unique_ptr<int> x(data);
      std::this_thread::sleep_for(100ms);
      *x *= *x;  // Compute square
      return x.release();
    }
    return GO_ON;
  }
};

struct SumCollector : ff::ff_node_t<int, void> {
  auto svc(int *data) -> void * override {
    std::unique_ptr<int> square(static_cast<int *>(data));
    if (square != nullptr) {
      counter.fetch_add(*square, std::memory_order_relaxed);
    }
    return GO_ON;
  }
};

auto main() -> int {
  ff::ff_farm farm;

  constexpr auto NUM_WORKERS = 16;
  auto workers = std::vector<ff::ff_node *>();

  std::generate_n(std::back_inserter(workers), NUM_WORKERS, []() {
    return new SquareWorker();
  });

  farm.add_workers(workers);
  farm.add_emitter(new IntegerEmitter(100));
  farm.add_collector(new SumCollector());

  farm.run_and_wait_end();

  std::print("Count: {}", counter.load());
  return 0;
}
