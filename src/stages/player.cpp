#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <iostream>
#include <vector>

namespace stages
{
  static std::vector<size_t> collect_children(int fd, size_t addr, size_t cs, size_t ce)
  {
    std::vector<size_t> out;
    auto head = memory::read<size_t>(fd, addr + cs).value_or(0);
    if (head < 0x10000)
      return out;
    auto first = memory::read<size_t>(fd, head).value_or(0);
    auto last = memory::read<size_t>(fd, head + ce).value_or(0);
    if (first < 0x10000 || last < 0x10000)
      return out;
    size_t node = first;
    for (int i = 0; i < 500; ++i)
    {
      if (node == last || node == 0)
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

  void player(int fd)
  {
    std::cerr << "[player]\n";

    size_t dm_addr = G_DATA_MODEL_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    std::optional<size_t> players_addr;
    if (cs > 0 && ce > 0)
    {
      auto dm_kids = collect_children(fd, dm_addr, cs, ce);
      for (size_t c : dm_kids)
      {
        if (auto r = rtti::scan_rtti(fd, c))
        {
          if (r->name == "Players@RBX")
          {
            players_addr = c;
            break;
          }
        }
      }
    }

    if (!players_addr)
    {
      const size_t ranges[] = { 0x2000, 0x4000, 0x8000 };
      for (size_t range : ranges)
      {
        if (auto off = rtti::find(fd, dm_addr, "Players@RBX", range, 8))
        {
          if (auto pa = memory::read<size_t>(fd, dm_addr + *off))
          {
            players_addr = *pa;
            break;
          }
        }
      }
    }

    if (!players_addr)
    {
      for (size_t off = 0; off < 0x2000; off += 8)
      {
        auto ptr = memory::read<size_t>(fd, dm_addr + off);
        if (!ptr || *ptr < 0x10000)
          continue;
        if (auto r = rtti::scan_rtti(fd, *ptr))
        {
          if (r->name == "Players@RBX")
          {
            players_addr = *ptr;
            break;
          }
        }
      }
    }

    if (!players_addr)
    {
      std::cerr << "  Players not found\n";
      return;
    }

    size_t pa = *players_addr;
    std::cerr << "  Players @ 0x" << std::hex << pa << std::dec << "\n";

    auto lp_off = rtti::find(fd, pa, "Player@RBX", 0x1000, 8);
    if (!lp_off)
    {
      std::cerr << "  LocalPlayer not found\n";
      return;
    }
    G_DUMPER.add_offset("Players", "LocalPlayer", *lp_off);

    auto lp_addr_opt = memory::read<size_t>(fd, pa + *lp_off);
    if (!lp_addr_opt)
    {
      std::cerr << "  Failed to read LocalPlayer addr\n";
      return;
    }
    size_t lp_addr = *lp_addr_opt;
    std::cerr << "  LocalPlayer @ 0x" << std::hex << lp_addr << std::dec << "\n";

    const char *rtti_names[] = { "ModelInstance@RBX", "Model@RBX" };
    for (const char *rtti_name : rtti_names)
    {
      if (auto ch = rtti::find(fd, lp_addr, rtti_name, 0x1000, 8))
      {
        G_DUMPER.add_offset("Player", "Character", *ch);
        std::cerr << "  Character at +0x" << std::hex << *ch << std::dec << "\n";
        break;
      }
    }

    if (auto tm = rtti::find(fd, lp_addr, "Team@RBX", 0x400, 8))
    {
      G_DUMPER.add_offset("Player", "Team", *tm);
      std::cerr << "  Team at +0x" << std::hex << *tm << std::dec << "\n";

      if (auto team_addr = memory::read<size_t>(fd, lp_addr + *tm))
      {
        if (*team_addr >= 0x10000)
        {
          for (size_t toff = 0; toff < 0x100; toff += 1)
          {
            auto v = memory::read<uint8_t>(fd, *team_addr + toff);
            if (!v)
              continue;
            auto v32 = memory::read<uint32_t>(fd, *team_addr + toff - (toff % 4)).value_or(0);
            if ((v32 & 0xFF) == *v && v32 < 0x10000)
            {
              G_DUMPER.add_offset("Team", "TeamColor", toff);
              std::cerr << "  Team::TeamColor at +0x" << std::hex << toff << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 8)
    {
      auto uid = memory::read<int64_t>(fd, lp_addr + off);
      if (!uid)
        continue;
      if (*uid > 0 && *uid < 100000000000LL)
      {
        auto as_usize = memory::read<size_t>(fd, lp_addr + off);
        if (!as_usize)
          continue;
        if (*as_usize < 0x10000 || *as_usize > 0x7fffffffffff)
        {
          G_DUMPER.add_offset("Player", "UserId", off);
          std::cerr << "  UserId at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 8)
    {
      auto ptr = memory::read<size_t>(fd, lp_addr + off);
      if (ptr && *ptr >= 0x10000)
      {
        if (auto s = memory::read_name_fmt(fd, *ptr))
        {
          if (s->length() >= 2 && s->length() <= 30 && s->find('@') == std::string::npos)
          {
            if (!G_DUMPER.get_offset("Player", "DisplayName"))
            {
              G_DUMPER.add_offset("Player", "DisplayName", off);
              std::cerr << "  DisplayName at +0x" << std::hex << off << std::dec << " ('" << *s << "')\n";
            }
          }
        }
      }
      if (G_DUMPER.get_offset("Player", "DisplayName"))
        break;

      if (auto s = read_sso(fd, lp_addr + off))
      {
        if (s->length() >= 2 && s->length() <= 30 && s->find('@') == std::string::npos && *s != "Player")
        {
          G_DUMPER.add_offset("Player", "DisplayName", off);
          std::cerr << "  DisplayName at +0x" << std::hex << off << std::dec << " ('" << *s << "')\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 1)
    {
      auto v = memory::read<uint8_t>(fd, lp_addr + off);
      if (!v)
        continue;
      if (*v > 0 && *v < 255)
      {
        auto v32 = memory::read<uint32_t>(fd, lp_addr + off - (off % 4));
        if (!v32)
          continue;
        if ((*v32 & 0xFF) == *v && *v32 < 0x1000)
        {
          G_DUMPER.add_offset("Player", "TeamColor", off);
          std::cerr << "  Player::TeamColor at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }
  }
} // namespace stages
