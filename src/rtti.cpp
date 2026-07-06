#include "rtti.hpp"
#include "memory.hpp"
#include <algorithm>
#include <vector>

namespace rtti
{
  bool is_valid_ptr(size_t ptr)
  {
    return ptr >= 0x10000 && ptr <= 0x7fffffffffff;
  }

  std::string demangle_itanium(const std::string &mangled)
  {
    if (mangled.empty() || mangled[0] != 'N')
    {
      return mangled;
    }

    std::vector<std::string> components;
    size_t pos = 1;

    while (pos < mangled.size() && mangled[pos] != 'E')
    {
      if (!std::isdigit(mangled[pos]))
      {
        return mangled;
      }

      size_t start = pos;
      while (pos < mangled.size() && std::isdigit(mangled[pos]))
      {
        pos++;
      }
      size_t len = std::stoull(mangled.substr(start, pos - start));
      if (len == 0 || len > 256)
        return mangled;

      if (pos + len > mangled.size())
        return mangled;
      components.push_back(mangled.substr(pos, len));
      pos += len;
    }

    if (components.empty())
      return mangled;

    std::string result;
    for (auto it = components.rbegin(); it != components.rend(); ++it)
    {
      if (!result.empty())
        result += "@";
      result += *it;
    }
    return result;
  }

  std::optional<RttiInfo> scan_rtti(int fd, size_t address)
  {
    auto vtable_ptr = memory::read<size_t>(fd, address);
    if (!vtable_ptr || !is_valid_ptr(*vtable_ptr))
      return std::nullopt;

    auto typeinfo_ptr = memory::read<size_t>(fd, *vtable_ptr - 8);
    if (!typeinfo_ptr || !is_valid_ptr(*typeinfo_ptr))
      return std::nullopt;

    auto name_ptr = memory::read<size_t>(fd, *typeinfo_ptr + 8);
    if (!name_ptr || !is_valid_ptr(*name_ptr))
      return std::nullopt;

    auto mangled = memory::read_string(fd, *name_ptr, 256);
    if (!mangled || mangled->empty())
      return std::nullopt;

    char first = (*mangled)[0];
    if (!std::isalnum(first) && first != 'N' && first != 'Z')
    {
      return std::nullopt;
    }

    return RttiInfo{ demangle_itanium(*mangled) };
  }

  // find rtti of all classes in the module
  void scan_section_batched(int fd, size_t section_start, size_t section_size, size_t module_base, size_t alignment,
                            std::function<void(size_t, const std::string &)> on_match)
  {
    const size_t page_size = 1024 * 1024; // 1MB
    std::vector<uint8_t> page_buf(page_size);

    size_t offset = 0;
    while (offset < section_size)
    {
      size_t remaining = section_size - offset;
      size_t to_read = std::min(page_size, remaining);

      size_t addr = section_start + offset;
      auto n_opt = memory::read_into(fd, addr, page_buf.data(), to_read);
      if (!n_opt)
      {
        offset += page_size;
        continue;
      }
      size_t n = *n_opt;

      size_t chunk_end = (n / alignment) * alignment;
      size_t i = 0;
      while (i + 8 <= chunk_end)
      {
        size_t ptr_val = *reinterpret_cast<size_t *>(&page_buf[i]);

        if (is_valid_ptr(ptr_val))
        {
          if (auto rtti = scan_rtti(fd, ptr_val))
          {
            size_t module_off = (addr + i) - module_base;
            on_match(module_off, rtti->name);
          }
        }
        i += alignment;
      }

      offset += page_size;
    }
  }

  // fine the value of a property of a class
  std::optional<size_t> find(int fd, size_t base_address, const std::string &target_class, size_t max_offset, size_t alignment)
  {
    size_t offset = 0;
    while (offset < max_offset)
    {
      size_t addr = base_address + offset;
      auto ptr_val = memory::read<size_t>(fd, addr);
      if (!ptr_val || !is_valid_ptr(*ptr_val))
      {
        offset += alignment;
        continue;
      }

      if (auto rtti = scan_rtti(fd, *ptr_val))
      {
        if (rtti->name == target_class)
        {
          return offset;
        }
      }
      offset += alignment;
    }
    return std::nullopt;
  }

} // namespace rtti
