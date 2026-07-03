#include "memory.hpp"
#include <cmath>
#include <unistd.h>

namespace memory
{
  bool pread_exact(int fd, void *buf, size_t size, size_t address)
  {
    ssize_t n = ::pread64(fd, buf, size, address);
    return n >= 0 && static_cast<size_t>(n) == size;
  }

  std::optional<std::vector<uint8_t>> read_bytes(int fd, size_t address, size_t size)
  {
    std::vector<uint8_t> buf(size, 0);
    if (pread_exact(fd, buf.data(), size, address))
    {
      return buf;
    }
    return std::nullopt;
  }

  std::optional<size_t> read_into(int fd, size_t address, void *buf, size_t size)
  {
    ssize_t n = ::pread64(fd, buf, size, address);
    if (n >= 0)
    {
      return static_cast<size_t>(n);
    }
    return std::nullopt;
  }

  std::optional<std::string> read_string(int fd, size_t address, size_t max_len)
  {
    auto buf_opt = read_bytes(fd, address, max_len);
    if (!buf_opt)
      return std::nullopt;
    const auto &buf = *buf_opt;
    size_t end = 0;
    while (end < buf.size() && buf[end] != 0)
    {
      end++;
    }
    std::string s(reinterpret_cast<const char *>(buf.data()), end);
    if (s.empty())
      return std::nullopt;
    return s;
  }

  std::optional<std::string> read_name_fmt(int fd, size_t address)
  {
    auto ctrl_opt = read<uint8_t>(fd, address);
    if (!ctrl_opt)
      return std::nullopt;
    uint8_t ctrl = *ctrl_opt;
    bool is_long = (ctrl & 1) != 0;
    size_t len = ctrl >> 1;
    if (len == 0)
      return std::nullopt;

    if (is_long)
    {
      auto ptr_opt = read<size_t>(fd, address);
      if (!ptr_opt)
        return std::nullopt;
      size_t ptr = *ptr_opt;
      size_t ptr_cleared = ptr & ~1ULL;
      return read_string(fd, ptr_cleared, len);
    }
    else
    {
      return read_string(fd, address + 1, len);
    }
  }

  std::optional<float> read_f32(int fd, size_t address)
  {
    auto v_opt = read<float>(fd, address);
    if (!v_opt)
      return std::nullopt;
    float v = *v_opt;
    if (std::isnan(v) || std::isinf(v) || std::fpclassify(v) == FP_SUBNORMAL)
      return std::nullopt;
    return v;
  }

  std::optional<std::array<float, 3>> read_f32x3(int fd, size_t address)
  {
    auto v_opt = read<std::array<float, 3>>(fd, address);
    if (!v_opt)
      return std::nullopt;
    auto &v = *v_opt;
    for (float x : v)
    {
      if (std::isnan(x) || std::isinf(x) || std::fpclassify(x) == FP_SUBNORMAL)
        return std::nullopt;
    }
    return v;
  }

  bool check_f32(float v)
  {
    return !std::isnan(v) && !std::isinf(v) && std::fpclassify(v) != FP_SUBNORMAL;
  }

  std::optional<int64_t> read_i64_sane(int fd, size_t address)
  {
    auto v_opt = read<int64_t>(fd, address);
    if (!v_opt)
      return std::nullopt;
    int64_t v = *v_opt;
    if (v < -10000000 || v > 10000000)
      return std::nullopt;
    return v;
  }
} // namespace memory
