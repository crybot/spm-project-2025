#include "record_loader.hpp"

files::RecordLoader::RecordLoader(const std::filesystem::path& path)
    : filestream_{path, std::ios::binary} {
}

auto files::RecordLoader::readNext() -> std::optional<files::Record> {
  auto records = std::vector<Record>{};
  uint64_t key;

  if (filestream_.read(reinterpret_cast<char*>(&key), sizeof(uint64_t))) {
    uint32_t p_len;
    filestream_.read(reinterpret_cast<char*>(&p_len), sizeof(uint32_t));

    auto payload = std::vector<char>(p_len);
    if (p_len > 0) {
      filestream_.read(payload.data(), p_len);
    }
    return std::make_optional<Record>(key, std::move(payload));
  }
  return {};
}
