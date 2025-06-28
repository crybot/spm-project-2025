// clang-format off
#include <ff/ff.hpp>       // Must be first
// clang-format on

#include <algorithm>
#include <ff/farm.hpp>
#include <ff/node.hpp>
#include <iterator>
#include <memory>
// #include <print>
#include <random>
#include <thread>

using namespace std::chrono_literals;
const int MAX_VALUE = 10;

struct IntegerEmitter : ff::ff_node_t<int> {
  IntegerEmitter(int iterations)
      : ITERATIONS(iterations), rnd_device_(), gen_(rnd_device_()), distrib_(1, MAX_VALUE) {
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

struct SquareWorker : ff::ff_node_t<int, void> {
  SquareWorker() noexcept = default;

  auto svc(int *data) -> void * override {
    if (data != nullptr) {
      std::unique_ptr<int> x(data);
      // std::print(">> {}\n", *x * *x);
      std::this_thread::sleep_for(100ms);
    }
    return GO_ON;
  }
};

auto main() -> int {
  constexpr auto NUM_WORKERS = 4;
  constexpr auto NUM_TASKS = 100;

  auto workers = std::vector<std::unique_ptr<ff::ff_node>>();
  std::generate_n(std::back_inserter(workers), NUM_WORKERS, std::make_unique<SquareWorker>);

  auto emitter = std::make_unique<IntegerEmitter>(NUM_TASKS);

  ff::ff_Farm<int, void> farm(std::move(workers), std::move(emitter), nullptr);
  farm.remove_collector();
  farm.run_and_wait_end();

  return 0;
}
