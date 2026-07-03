#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <iostream>

namespace stages
{
  static std::optional<size_t> find_parent_offset(int fd, size_t addr)
  {
    return rtti::find(fd, addr, "DataModel@RBX", 0x400, 8);
  }

  static std::optional<std::string> read_sso(int fd, size_t addr)
  {
    auto size_byte = memory::read<uint8_t>(fd, addr);
    if (!size_byte)
      return std::nullopt;
    size_t len = *size_byte;

    if (len <= 15)
    {
      auto buf = memory::read_bytes(fd, addr + 1, 15);
      if (!buf)
        return std::nullopt;
      size_t end = 0;
      while (end < buf->size() && (*buf)[end] != 0)
      {
        end++;
      }
      if (end > len)
        end = len;
      std::string s(reinterpret_cast<const char *>(buf->data()), end);
      if (s.length() == len)
        return s;
      return std::nullopt;
    }
    else
    {
      auto ptr = memory::read<size_t>(fd, addr + 8);
      auto len2 = memory::read<size_t>(fd, addr + 16);
      if (!ptr || !len2 || *ptr < 0x10000 || *len2 > 256)
        return std::nullopt;
      return memory::read_string(fd, *ptr, *len2);
    }
  }

  static std::optional<std::pair<size_t, size_t>> try_children_verified(int fd, size_t addr, size_t parent_off)
  {
    for (size_t start_off = 0; start_off < 0x300; start_off += 8)
    {
      if (start_off == parent_off)
        continue;
      auto start_ptr = memory::read<size_t>(fd, addr + start_off);
      if (!start_ptr || *start_ptr < 0x10000)
        continue;

      for (size_t end_off = 0; end_off < 0x20; end_off += 8)
      {
        auto end_ptr = memory::read<size_t>(fd, *start_ptr + end_off);
        if (!end_ptr || *end_ptr < 0x10000)
          continue;

        auto node = memory::read<size_t>(fd, *start_ptr);
        if (!node || *node < 0x10000)
          continue;

        uint32_t valid = 0;
        size_t n = *node;
        bool failed = false;
        for (int i = 0; i < 500; ++i)
        {
          if (n == *end_ptr)
            break;
          auto child = memory::read<size_t>(fd, n);
          if (!child || *child < 0x10000)
          {
            failed = true;
            break;
          }
          auto vtable = memory::read<size_t>(fd, *child);
          if (!vtable || *vtable < 0x10000)
          {
            failed = true;
            break;
          }
          auto parent = memory::read<size_t>(fd, *child + parent_off);
          if (!parent || *parent != addr)
          {
            failed = true;
            break;
          }
          valid++;
          n += 0x10;
        }
        if (!failed && valid >= 2)
        {
          return std::make_pair(start_off, end_off);
        }
      }
    }
    return std::nullopt;
  }

  static std::optional<std::pair<size_t, size_t>> try_children_no_verify(int fd, size_t addr)
  {
    for (size_t start_off = 0; start_off < 0x300; start_off += 8)
    {
      auto start_ptr = memory::read<size_t>(fd, addr + start_off);
      if (!start_ptr || *start_ptr < 0x10000)
        continue;

      for (size_t end_off = 0; end_off < 0x20; end_off += 8)
      {
        auto end_ptr = memory::read<size_t>(fd, *start_ptr + end_off);
        if (!end_ptr || *end_ptr < 0x10000)
          continue;

        auto node = memory::read<size_t>(fd, *start_ptr);
        if (!node || *node < 0x10000)
          continue;

        uint32_t valid = 0;
        size_t n = *node;
        bool failed = false;
        for (int i = 0; i < 500; ++i)
        {
          if (n == *end_ptr)
            break;
          auto child = memory::read<size_t>(fd, n);
          if (!child || *child < 0x10000)
          {
            failed = true;
            break;
          }
          auto vtable = memory::read<size_t>(fd, *child);
          if (!vtable || *vtable < 0x10000)
          {
            failed = true;
            break;
          }

          if (rtti::scan_rtti(fd, *child))
          {
            valid++;
          }
          else
          {
            failed = true;
            break;
          }
          n += 0x10;
        }
        if (!failed && valid >= 2)
        {
          return std::make_pair(start_off, end_off);
        }
      }
    }
    return std::nullopt;
  }

  static std::optional<std::pair<size_t, size_t>> try_children_bruteforce(int fd, size_t addr, size_t parent_off)
  {
    const size_t strides[] = {0x10, 0x18, 0x20, 0x08};
    for (size_t start_off = 0; start_off < 0x300; start_off += 8)
    {
      if (start_off == parent_off)
        continue;
      auto head_ptr = memory::read<size_t>(fd, addr + start_off);
      if (!head_ptr || *head_ptr < 0x10000)
        continue;

      for (size_t stride : strides)
      {
        for (size_t end_off = 0; end_off < 0x20; end_off += 8)
        {
          auto sentinel = memory::read<size_t>(fd, *head_ptr + end_off);
          if (!sentinel)
            continue;
          auto first = memory::read<size_t>(fd, *head_ptr);
          if (!first || *first < 0x10000)
            continue;

          uint32_t valid = 0;
          size_t n = *first;
          bool failed = false;
          for (int i = 0; i < 500; ++i)
          {
            if (n == *sentinel || n == 0 || n == *head_ptr)
              break;
            auto child = memory::read<size_t>(fd, n);
            if (!child || *child < 0x10000)
            {
              failed = true;
              break;
            }
            auto vtable = memory::read<size_t>(fd, *child);
            if (!vtable || *vtable < 0x10000)
            {
              failed = true;
              break;
            }
            auto parent = memory::read<size_t>(fd, *child + parent_off);
            if (!parent || *parent != addr)
            {
              failed = true;
              break;
            }
            valid++;
            n += stride;
          }
          if (!failed && valid >= 1)
          {
            return std::make_pair(start_off, end_off);
          }
        }
      }
    }
    return std::nullopt;
  }

  static std::optional<size_t> find_class_descriptor(int fd, size_t addr)
  {
    for (size_t off = 0; off < 0x80; off += 8)
    {
      auto ptr = memory::read<size_t>(fd, addr + off);
      if (!ptr || *ptr < 0x10000)
        continue;
      auto vtable = memory::read<size_t>(fd, *ptr);
      if (!vtable || *vtable < 0x10000 || *vtable > 0x7fffffffffff)
        continue;
      G_DUMPER.add_offset("Instance", "ClassDescriptor", off);
      return off;
    }
    return std::nullopt;
  }

  static std::optional<size_t> find_class_name(int fd, size_t addr)
  {
    if (auto rtti = rtti::scan_rtti(fd, addr))
    {
      std::string class_name = rtti->name;
      size_t pos = class_name.find('@');
      if (pos != std::string::npos)
      {
        class_name = class_name.substr(0, pos);
      }

      for (size_t off = 0; off < 0x80; off += 8)
      {
        auto ptr = memory::read<size_t>(fd, addr + off);
        if (ptr && *ptr >= 0x10000)
        {
          if (auto s = memory::read_name_fmt(fd, *ptr))
          {
            if (*s == class_name)
            {
              G_DUMPER.add_offset("Instance", "ClassName", off);
              return off;
            }
          }
        }
        if (auto s = read_sso(fd, addr + off))
        {
          if (*s == class_name)
          {
            G_DUMPER.add_offset("Instance", "ClassName", off);
            return off;
          }
        }
      }
    }
    return std::nullopt;
  }

  bool instance(int fd)
  {
    std::cerr << "[instance]\n";

    size_t dm_addr = G_DATA_MODEL_ADDR;
    auto ws_off = G_DUMPER.get_offset("DataModel", "Workspace");
    if (!ws_off)
    {
      std::cerr << "No Workspace offset\n";
      return false;
    }

    auto ws_addr = memory::read<size_t>(fd, dm_addr + *ws_off);
    if (!ws_addr)
    {
      std::cerr << "Failed to read Workspace\n";
      return false;
    }
    std::cerr << "  Workspace @ 0x" << std::hex << *ws_addr << std::dec << "\n";
    G_WORKSPACE_ADDR = *ws_addr;

    for (size_t off = 0; off < 0x400; off += 8)
    {
      auto ptr = memory::read<size_t>(fd, *ws_addr + off);
      if (ptr && *ptr >= 0x10000)
      {
        if (auto s = memory::read_name_fmt(fd, *ptr))
        {
          if (*s == "Workspace")
          {
            G_DUMPER.add_offset("Instance", "Name", off);
            break;
          }
        }
      }
      if (auto s = read_sso(fd, *ws_addr + off))
      {
        if (*s == "Workspace")
        {
          G_DUMPER.add_offset("Instance", "Name", off);
          break;
        }
      }
    }

    size_t parent_off = find_parent_offset(fd, *ws_addr).value_or(0x70);

    auto children = try_children_verified(fd, *ws_addr, parent_off);
    if (!children)
      children = try_children_no_verify(fd, *ws_addr);
    if (!children)
      children = try_children_bruteforce(fd, *ws_addr, parent_off);

    if (children)
    {
      G_DUMPER.add_offset("Instance", "ChildrenStart", children->first);
      G_DUMPER.add_offset("Instance", "ChildrenEnd", children->second);
    }
    else
    {
      std::cerr << "  ChildrenStart/End not found via linked-list walk\n";
    }

    find_class_descriptor(fd, *ws_addr);
    find_class_name(fd, *ws_addr);

    if (parent_off != 0x70)
    {
      G_DUMPER.add_offset("Instance", "Parent", parent_off);
    }

    return true;
  }
} // namespace stages
