#include <algorithm>
#include <ff/ff.hpp>
#include <iostream>
#include <iterator>
// #include <print>
#include <random>
#include <thread>
// #include <ff/pipeline.hpp>
// #include <ff/node.hpp>
// #include <ff/farm.hpp>
// #include <ff/farm.hpp>

using namespace std::chrono_literals;
const int MAX_VALUE = 10;


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
  ff::ff_pipeline pipe;
  ff::ff_farm farm;

  constexpr auto NUM_WORKERS = 4;
  auto workers = std::vector<ff::ff_node *>();

  std::generate_n(std::back_inserter(workers), NUM_WORKERS, []() {
    return new SquareWorker();
  });

  farm.add_workers(workers);

  // You could also use farm.add_emitter() instead of a pipeline, but we use it here for demostration purposes
  pipe.add_stage(new IntegerEmitter(100));  // Stage 0
  pipe.add_stage(farm);                     // Stage 1

  std::cout << "Starting pipeline...\n";
  pipe.run_and_wait_end();
  std::cout << "Pipeline done.\n";
  return 0;
}
