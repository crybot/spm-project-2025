#include <charconv>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class CliParser {
 public:
  CliParser(int argc, char* argv[]) : program_name_(*argv) {
    parseArguments(std::span(argv + 1, argc - 1));
  }

  template <typename T>
  auto get(const std::string& name) const -> std::optional<T> {
    auto it = options_.find(name);
    if (it == options_.end()) {
      return std::nullopt;
    }

    if constexpr (std::is_same_v<T, bool>) {
      return it->second.has_value() ? std::optional<T>(tryParse<T>(*it->second))
                                    : std::optional<T>(true);
    }
    else {
      if (!it->second.has_value()) {
        throw std::runtime_error("Option '" + name + "' requires a value.");
      }
      return std::optional<T>(tryParse<T>(*it->second));
    }
  }

  template <typename T>
  std::optional<T> get(size_t index) const {
    if (index >= positional_args_.size()) {
      return std::nullopt;
    }
    return std::optional<T>(tryParse<T>(positional_args_[index]));
  }

 private:
  std::string_view program_name_;
  std::unordered_map<std::string_view, std::optional<std::string_view>> options_;
  std::vector<std::string_view> positional_args_;

  auto parseArguments(std::span<char*> args) -> void {
    for (size_t i = 0; i < args.size(); ++i) {
      std::string_view arg = args[i];
      if (arg.starts_with("--")) {
        auto option_name = arg.substr(2);
        if (i + 1 < args.size() && !std::string_view(args[i + 1]).starts_with("--")) {
          options_[option_name] = std::string_view(args[++i]);
        }
        else {
          options_[option_name] = std::nullopt;  // A flag
        }
      }
      else {
        positional_args_.emplace_back(arg);
      }
    }
  }

  template <typename T>
  auto tryParse(std::string_view value_str) const -> T {
    T value;
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
      auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), value);
      if (result.ec != std::errc()) {
        throw std::runtime_error("Failed to parse integer value.");
      }
    }
    else if constexpr (std::is_floating_point_v<T>) {
      auto result = std::from_chars(value_str.data(), value_str.data() + value_str.size(), value);
      if (result.ec != std::errc()) {
        throw std::runtime_error("Failed to parse floating-point value.");
      }
    }
    else if constexpr (std::is_same_v<T, std::string>) {
      return std::string(value_str);
    }
    else if constexpr (std::is_same_v<T, bool>) {
      if (value_str == "true" || value_str == "1")
        return true;
      if (value_str == "false" || value_str == "0")
        return false;
      throw std::runtime_error("Invalid boolean value.");
    }
    return value;
  }
};
