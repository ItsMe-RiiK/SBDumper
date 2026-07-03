#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace memory
{
  bool pread_exact(int fd, void *buf, size_t size, size_t address);

  template <typename T> std::optional<T> read(int fd, size_t address)
  {
    T val{};
    if (pread_exact(fd, &val, sizeof(T), address))
    {
      return val;
    }
    return std::nullopt;
  }

  std::optional<std::vector<uint8_t>> read_bytes(int fd, size_t address, size_t size);

  std::optional<size_t> read_into(int fd, size_t address, void *buf, size_t size);

  std::optional<std::string> read_string(int fd, size_t address, size_t max_len);

  std::optional<std::string> read_name_fmt(int fd, size_t address);

  std::optional<float> read_f32(int fd, size_t address);

  std::optional<std::array<float, 3>> read_f32x3(int fd, size_t address);

  bool check_f32(float v);

  template <typename T> std::optional<T> read_any(int fd, size_t address)
  {
    return read<T>(fd, address);
  }

  std::optional<int64_t> read_i64_sane(int fd, size_t address);

} // namespace memory
