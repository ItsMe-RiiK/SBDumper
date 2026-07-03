#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <algorithm>
#include <iostream>

namespace stages
{
  static std::optional<size_t> find_int64(int fd, size_t addr, size_t max_off, const std::vector<size_t> &skip)
  {
    for (size_t off = 8; off < max_off; off += 8)
    {
      if (std::find(skip.begin(), skip.end(), off) != skip.end())
        continue;
      auto v = memory::read<int64_t>(fd, addr + off);
      if (!v)
        continue;
      if (*v > 0 && *v < 100000000000LL)
      {
        auto next = memory::read<int64_t>(fd, addr + off + 8).value_or(-1);
        auto prev = memory::read<int64_t>(fd, addr + off - 8).value_or(-1);
        if (next < 0 || next > 100000000000LL || prev < 0 || prev > 100000000000LL)
        {
          continue;
        }
        return off;
      }
    }
    return std::nullopt;
  }

  bool data_model(int fd)
  {
    std::cerr << "[data_model]\n";

    size_t ve = G_VISUAL_ENGINE;
    auto fdm_off = G_DUMPER.get_offset("VisualEngine", "FakeDataModel");
    if (!fdm_off)
    {
      std::cerr << "No FakeDataModel offset\n";
      return false;
    }

    auto fdm = memory::read<size_t>(fd, ve + *fdm_off);
    if (!fdm)
    {
      std::cerr << "Failed to read FakeDataModel ptr\n";
      return false;
    }

    auto dm_off = rtti::find(fd, *fdm, "DataModel@RBX", 0x1000, 8);
    if (!dm_off)
    {
      std::cerr << "RealDataModel offset not found\n";
      return false;
    }

    G_DUMPER.add_offset("FakeDataModel", "RealDataModel", *dm_off);

    auto dm_addr = memory::read<size_t>(fd, *fdm + *dm_off);
    if (!dm_addr)
    {
      std::cerr << "Failed to read RealDataModel addr\n";
      return false;
    }

    G_DATA_MODEL_ADDR = *dm_addr;
    std::cerr << "  DataModel @ 0x" << std::hex << *dm_addr << std::dec << "\n";

    if (auto ws = rtti::find(fd, *dm_addr, "Workspace@RBX", 0x1000, 8))
    {
      G_DUMPER.add_offset("DataModel", "Workspace", *ws);
    }

    if (auto pi = find_int64(fd, *dm_addr, 0x300, {}))
    {
      G_DUMPER.add_offset("DataModel", "PlaceId", *pi);
      std::cerr << "  PlaceId at +0x" << std::hex << *pi << std::dec << "\n";
    }

    std::vector<size_t> skip;
    if (auto pi = G_DUMPER.get_offset("DataModel", "PlaceId"))
      skip.push_back(*pi);
    if (auto ws = G_DUMPER.get_offset("DataModel", "Workspace"))
      skip.push_back(*ws);

    if (auto id = find_int64(fd, *dm_addr, 0x300, skip))
    {
      G_DUMPER.add_offset("DataModel", "CreatorId", *id);
      std::cerr << "  CreatorId at +0x" << std::hex << *id << std::dec << "\n";
      skip.push_back(*id);
    }
    if (auto id = find_int64(fd, *dm_addr, 0x300, skip))
    {
      G_DUMPER.add_offset("DataModel", "GameId", *id);
      std::cerr << "  GameId at +0x" << std::hex << *id << std::dec << "\n";
      skip.push_back(*id);
    }

    for (size_t off = 0; off < 0x200; off += 8)
    {
      if (std::find(skip.begin(), skip.end(), off) != skip.end())
        continue;
      auto ptr = memory::read<size_t>(fd, *dm_addr + off);
      if (!ptr || *ptr < 0x10000)
        continue;

      if (auto s = memory::read_string(fd, *ptr, 64))
      {
        if (s->size() == 36 && (*s)[8] == '-' && (*s)[13] == '-' && (*s)[18] == '-' && (*s)[23] == '-')
        {
          G_DUMPER.add_offset("DataModel", "JobId", off);
          std::cerr << "  JobId at +0x" << std::hex << off << std::dec << "\n";
          skip.push_back(off);
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x700; off += 8)
    {
      if (std::find(skip.begin(), skip.end(), off) != skip.end())
        continue;
      auto ptr = memory::read<size_t>(fd, *dm_addr + off);
      if (!ptr || *ptr < 0x10000)
        continue;

      if (auto s = memory::read_string(fd, *ptr, 64))
      {
        if (s->find(':') != std::string::npos && std::count(s->begin(), s->end(), '.') >= 3)
        {
          G_DUMPER.add_offset("DataModel", "ServerIp", off);
          std::cerr << "  ServerIp at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    return true;
  }
} // namespace stages
