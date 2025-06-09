#include <chrono>
#include <print>
#include <string_view>

template <typename Duration = std::chrono::milliseconds>
class StopWatch {
 public:
  StopWatch(std::string_view description = "Benchmark results", bool print_last = true)
      : description_{std::move(description)},
        begin_time_{std::chrono::high_resolution_clock::now()},
        print_last_{print_last} {
  }
  ~StopWatch() {
    if (print_last_) {
      printElapsed();
    }
  }

  auto reset() -> void {
    printElapsed();
    begin_time_ = std::chrono::high_resolution_clock::now();
  }

 private:
  std::string description_;
  std::chrono::high_resolution_clock::time_point begin_time_;
  bool print_last_;

  auto printElapsed() -> void {
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<Duration>(end_time - begin_time_);

    std::print("{}\nElapsed time ({}): {}\n\n", description_, getDurationSuffix(), elapsed.count());
  }

  static consteval auto getDurationSuffix() -> const char* {
    if constexpr (std::is_same_v<Duration, std::chrono::nanoseconds>)
      return "ns";
    if constexpr (std::is_same_v<Duration, std::chrono::microseconds>)
      return "us";
    if constexpr (std::is_same_v<Duration, std::chrono::milliseconds>)
      return "ms";
    if constexpr (std::is_same_v<Duration, std::chrono::seconds>)
      return "s";
    if constexpr (std::is_same_v<Duration, std::chrono::minutes>)
      return "min";
    if constexpr (std::is_same_v<Duration, std::chrono::hours>)
      return "h";
    else
      return "unit";
  }
};
