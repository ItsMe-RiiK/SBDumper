#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace stages
{
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
        end++;
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

  static std::vector<size_t> collect_children(int fd, size_t addr, size_t cs, size_t ce)
  {
    std::vector<size_t> out;
    auto head = memory::read<size_t>(fd, addr + cs);
    if (!head || *head < 0x10000)
      return out;
    auto first = memory::read<size_t>(fd, *head);
    auto last = memory::read<size_t>(fd, *head + ce);
    if (!first || !last || *first < 0x10000 || *last < 0x10000)
      return out;

    size_t node = *first;
    for (int i = 0; i < 500; ++i)
    {
      if (node == *last || node == 0)
        break;
      if (auto child = memory::read<size_t>(fd, node))
      {
        if (*child >= 0x10000)
          out.push_back(*child);
      }
      node += 0x10;
    }
    return out;
  }

  static std::vector<size_t> find_mesh_parts(int fd, size_t ws_addr, size_t cs, size_t ce)
  {
    std::vector<size_t> out;
    if (cs > 0 && ce > 0)
    {
      auto ws_kids = collect_children(fd, ws_addr, cs, ce);
      for (size_t child : ws_kids)
      {
        if (auto r = rtti::scan_rtti(fd, child))
        {
          if (r->name == "MeshPart@RBX")
          {
            out.push_back(child);
            if (out.size() >= 3)
              return out;
          }
        }
        auto grandkids = collect_children(fd, child, cs, ce);
        for (size_t gk : grandkids)
        {
          if (auto r = rtti::scan_rtti(fd, gk))
          {
            if (r->name == "MeshPart@RBX")
            {
              out.push_back(gk);
              if (out.size() >= 3)
                return out;
            }
          }
        }
      }
    }
    if (out.empty())
    {
      for (size_t off = 0; off < 0x4000; off += 8)
      {
        auto ptr = memory::read<size_t>(fd, ws_addr + off);
        if (!ptr || *ptr < 0x10000)
          continue;
        if (auto r = rtti::scan_rtti(fd, *ptr))
        {
          if (r->name == "MeshPart@RBX")
          {
            out.push_back(*ptr);
            if (out.size() >= 3)
              break;
          }
        }
      }
    }
    return out;
  }

  void mesh_part(int fd)
  {
    std::cerr << "[mesh_part]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    auto mesh_parts = find_mesh_parts(fd, ws_addr, cs, ce);
    if (mesh_parts.empty())
    {
      std::cerr << "  No MeshPart found\n";
      return;
    }
    std::cerr << "  Found " << mesh_parts.size() << " MeshPart(s)\n";

    size_t mp = mesh_parts[0];

    for (size_t off = 0; off < 0x200; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, mp + off))
      {
        if (*v >= 1 && *v <= 3)
        {
          if (auto v4 = memory::read<uint32_t>(fd, mp + off - (off % 4)))
          {
            if ((*v4 & 0xFF) == *v && *v4 < 0x1000)
            {
              G_DUMPER.add_offset("MeshPart", "RenderFidelity", off);
              std::cerr << "  MeshPart::RenderFidelity at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, mp + off))
      {
        if (*v <= 3)
        {
          size_t rf = G_DUMPER.get_offset("MeshPart", "RenderFidelity").value_or(-1);
          if (auto v4 = memory::read<uint32_t>(fd, mp + off - (off % 4)))
          {
            if (off != rf && (*v4 & 0xFF) == *v && *v4 < 0x1000)
            {
              G_DUMPER.add_offset("MeshPart", "CollisionFidelity", off);
              std::cerr << "  MeshPart::CollisionFidelity at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 8)
    {
      if (auto ptr = memory::read<size_t>(fd, mp + off))
      {
        if (*ptr >= 0x10000)
        {
          if (auto s = memory::read_name_fmt(fd, *ptr))
          {
            if (s->rfind("rbxasset", 0) == 0 || s->rfind("http", 0) == 0)
            {
              G_DUMPER.add_offset("MeshPart", "MeshId", off);
              std::cerr << "  MeshPart::MeshId at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
          if (auto s = read_sso(fd, mp + off))
          {
            if (s->rfind("rbxasset", 0) == 0 || s->rfind("http", 0) == 0)
            {
              G_DUMPER.add_offset("MeshPart", "MeshId", off);
              std::cerr << "  MeshPart::MeshId at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 8)
    {
      if (auto ptr = memory::read<size_t>(fd, mp + off))
      {
        if (*ptr >= 0x10000)
        {
          size_t mid = G_DUMPER.get_offset("MeshPart", "MeshId").value_or(-1);
          if (off == mid)
            continue;
          if (auto s = memory::read_name_fmt(fd, *ptr))
          {
            if (s->rfind("rbxasset", 0) == 0 || s->rfind("http", 0) == 0)
            {
              G_DUMPER.add_offset("MeshPart", "TextureId", off);
              std::cerr << "  MeshPart::TextureId at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }
  }
} // namespace stages
