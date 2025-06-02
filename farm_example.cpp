#include <algorithm>
#include <ff/ff.hpp>
#include <format>
#include <iostream>
#include <iterator>
#include <print>
#include <random>
#include <thread>
// #include <ff/pipeline.hpp>
// #include <ff/node.hpp>
// #include <ff/farm.hpp>
// #include <ff/farm.hpp>

using namespace std::chrono_literals;
const int MAX_VALUE = 10;

struct IntegerEmitter : ff::ff_node {
  IntegerEmitter() : rnd_device_(), gen_(rnd_device_()), distrib_(1, MAX_VALUE) {
  }

  auto svc_init() -> int override {
    return 0;
  }

  auto svc(void *data) -> void * override {
    auto rnd = distrib_(gen_);
    return static_cast<void *>(new int(rnd));
  }

 private:
  std::random_device rnd_device_;
  std::mt19937 gen_;
  std::uniform_int_distribution<> distrib_;
};

struct SquareWorker : ff::ff_node {
  SquareWorker() noexcept = default;

  auto svc(void *data) -> void * override {
    if (data != nullptr) {
      auto x = static_cast<int *>(data);
      std::print(">> {}\n", *x * *x);
      delete x;
      std::this_thread::sleep_for(200ms);
    }
    return GO_ON;
  }
};

auto main() -> int {
  ff::ff_pipeline pipe;
  ff::ff_farm farm;

  auto num_workers = 5;
  auto workers = std::vector<ff::ff_node *>();

  std::generate_n(std::back_inserter(workers), num_workers, []() {
    return new SquareWorker();
  });

  farm.add_workers(workers);

  pipe.add_stage(new IntegerEmitter());  // Stage 0
  pipe.add_stage(farm);  // Stage 1

  std::cout << "Starting pipeline...\n";
  pipe.run_and_wait_end();
  std::cout << "Pipeline done.\n";
  return 0;
}
