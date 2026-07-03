#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace stages
{
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

  static void dump_mouse_service(int fd)
  {
    size_t dm_addr = G_DATA_MODEL_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    std::optional<size_t> mouse_svc;
    if (cs > 0 && ce > 0)
    {
      auto kids = collect_children(fd, dm_addr, cs, ce);
      for (size_t c : kids)
      {
        if (auto r = rtti::scan_rtti(fd, c))
        {
          if (r->name == "MouseService@RBX")
          {
            mouse_svc = c;
            break;
          }
        }
      }
    }

    if (mouse_svc)
    {
      size_t ms = *mouse_svc;
      std::cerr << "  MouseService @ 0x" << std::hex << ms << std::dec << "\n";
      if (auto io = rtti::find(fd, ms, "InputObject@RBX", 0x200, 8))
      {
        G_DUMPER.add_offset("MouseService", "InputObject", *io);
        std::cerr << "  >> MouseService::InputObject = 0x" << std::hex << *io << " (dynamic)\n" << std::dec;
      }
    }
  }

  static void dump_stats(int fd)
  {
    size_t dm_addr = G_DATA_MODEL_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    std::optional<size_t> stats_svc;
    if (cs > 0 && ce > 0)
    {
      auto kids = collect_children(fd, dm_addr, cs, ce);
      for (size_t c : kids)
      {
        if (auto r = rtti::scan_rtti(fd, c))
        {
          if (r->name == "Stats@RBX" || r->name == "StatsItem@RBX")
          {
            stats_svc = c;
            break;
          }
          else
          {
            auto grandkids = collect_children(fd, c, cs, ce);
            for (size_t gk : grandkids)
            {
              if (auto r2 = rtti::scan_rtti(fd, gk))
              {
                if (r2->name == "StatsItem@RBX")
                {
                  stats_svc = gk;
                  break;
                }
              }
            }
            if (stats_svc)
              break;
          }
        }
      }
    }

    if (stats_svc)
    {
      size_t stats = *stats_svc;
      std::cerr << "  Stats @ 0x" << std::hex << stats << std::dec << "\n";

      for (size_t off = 0; off < 0x300; off += 4)
      {
        if (auto v = memory::read_f32(fd, stats + off))
        {
          if (*v > 0.0f && *v < 1000000.0f)
          {
            G_DUMPER.add_offset("StatsItem", "Value", off);
            std::cerr << "  >> StatsItem::Value = 0x" << std::hex << off << " (dynamic)\n" << std::dec;
            break;
          }
        }
      }

      for (size_t off = 0; off < 0x500; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, stats + off))
        {
          if (*ptr >= 0x10000)
          {
            if (auto s = memory::read_name_fmt(fd, *ptr))
            {
              if (!s->empty() && s->length() < 64 && !G_DUMPER.get_offset("StatsItem", "Name"))
              {
                G_DUMPER.add_offset("StatsItem", "Name", off);
                std::cerr << "  >> StatsItem::Name = 0x" << std::hex << off << " (dynamic)\n" << std::dec;
              }
              else if (G_DUMPER.get_offset("StatsItem", "Name") && !G_DUMPER.get_offset("StatsItem", "DisplayName") && !s->empty() &&
                       s->length() < 64)
              {
                G_DUMPER.add_offset("StatsItem", "DisplayName", off);
                std::cerr << "  >> StatsItem::DisplayName = 0x" << std::hex << off << " (dynamic)\n" << std::dec;
                break;
              }
            }
          }
        }
      }

      size_t val_off = G_DUMPER.get_offset("StatsItem", "Value").value_or(0);
      if (val_off > 0)
      {
        for (size_t off = val_off + 4; off < val_off + 0x50; off += 4)
        {
          if (auto v = memory::read_f32(fd, stats + off))
          {
            if (*v >= 0.0f && *v < 1000000.0f)
            {
              if (!G_DUMPER.get_offset("StatsItem", "AvgValue"))
              {
                G_DUMPER.add_offset("StatsItem", "AvgValue", off);
                std::cerr << "  >> StatsItem::AvgValue = 0x" << std::hex << off << " (dynamic)\n" << std::dec;
              }
              else if (!G_DUMPER.get_offset("StatsItem", "AvgValuePrev"))
              {
                G_DUMPER.add_offset("StatsItem", "AvgValuePrev", off);
                std::cerr << "  >> StatsItem::AvgValuePrev = 0x" << std::hex << off << " (dynamic)\n" << std::dec;
                break;
              }
            }
          }
        }
      }
    }
  }

  void constants(int fd)
  {
    std::cerr << "[mouse/stats dynamic]\n";
    dump_mouse_service(fd);
    dump_stats(fd);
  }
} // namespace stages
