#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace rtti
{
  struct RttiInfo
  {
    std::string name;
  };

  bool is_valid_ptr(size_t ptr);

  std::string demangle_itanium(const std::string &mangled);

  std::optional<RttiInfo> scan_rtti(int fd, size_t address);

  void scan_section_batched(int fd, size_t section_start, size_t section_size, size_t module_base, size_t alignment,
                            std::function<void(size_t, const std::string &)> on_match);

  std::optional<size_t> find(int fd, size_t base_address, const std::string &target_class, size_t max_offset, size_t alignment);

} // namespace rtti