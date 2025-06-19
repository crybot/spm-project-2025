#include <chrono>
#include <print>
#include <string_view>

template <typename Duration = std::chrono::milliseconds>
class StopWatch {
 public:
  StopWatch(
      std::string_view description = "Benchmark results",
      bool print_last = true,
      bool compute_statistics = false
  )
      : description_{std::move(description)},
        begin_time_{std::chrono::high_resolution_clock::now()},
        print_last_{print_last},
        compute_statistics_{compute_statistics} {
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
  bool compute_statistics_;
  std::size_t count_{0};
  double mean_{0.0};

  auto printElapsed() -> void {
    const auto end_time = std::chrono::high_resolution_clock::now();
    const auto elapsed = std::chrono::duration_cast<Duration>(end_time - begin_time_);

    std::print("{}\nElapsed time ({}): {}\n\n", description_, getDurationSuffix(), elapsed.count());

    if (compute_statistics_) {
      count_ += 1;
      mean_ = (static_cast<double>(count_ - 1) * mean_ + elapsed.count()) / count_;
      std::print("{}\nRunning mean - elapsed time ({}): {}\n\n", description_, getDurationSuffix(), mean_);
    }
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
