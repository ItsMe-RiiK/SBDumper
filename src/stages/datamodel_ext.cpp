#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <iostream>
#include <vector>

namespace stages
{
  void datamodel_ext(int fd)
  {
    std::cerr << "[datamodel_ext]\n";

    size_t dm_addr = G_DATA_MODEL_ADDR;
    if (dm_addr < 0x10000)
      return;

    std::vector<size_t> skip = { G_DUMPER.get_offset("DataModel", "PlaceId").value_or(0),
                                 G_DUMPER.get_offset("DataModel", "CreatorId").value_or(0),
                                 G_DUMPER.get_offset("DataModel", "GameId").value_or(0),
                                 G_DUMPER.get_offset("DataModel", "JobId").value_or(0),
                                 G_DUMPER.get_offset("DataModel", "Workspace").value_or(0) };

    for (size_t off = 0; off < 0x400; off += 8)
    {
      bool in_skip = false;
      for (size_t s : skip)
        if (s == off)
          in_skip = true;
      if (in_skip)
        continue;

      if (auto v = memory::read<int64_t>(fd, dm_addr + off))
      {
        if (*v > 0 && *v < 100000000000LL)
        {
          int64_t prev = memory::read<int64_t>(fd, dm_addr + off - 8).value_or(-1);
          int64_t next = memory::read<int64_t>(fd, dm_addr + off + 8).value_or(-1);
          if ((prev < 0 || prev > 100000000000LL) && (next < 0 || next > 100000000000LL))
          {
            G_DUMPER.add_offset("DataModel", "UniverseId", off);
            std::cerr << "  DataModel::UniverseId at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }

    for (size_t off = 0; off < 0x800; off += 8)
    {
      bool in_skip = false;
      for (size_t s : skip)
        if (s == off)
          in_skip = true;
      if (in_skip)
        continue;

      if (auto ptr = memory::read<size_t>(fd, dm_addr + off))
      {
        if (*ptr >= 0x10000)
        {
          if (auto str = memory::read_string(fd, *ptr, 48))
          {
            if (str->length() == 36 && (*str)[8] == '-' && (*str)[13] == '-' && (*str)[18] == '-' && (*str)[23] == '-')
            {
              G_DUMPER.add_offset("DataModel", "PrivateServerId", off);
              std::cerr << "  DataModel::PrivateServerId at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x800; off += 8)
    {
      bool in_skip = false;
      for (size_t s : skip)
        if (s == off)
          in_skip = true;
      if (in_skip)
        continue;

      if (auto v = memory::read<int64_t>(fd, dm_addr + off))
      {
        auto ptr = memory::read<size_t>(fd, dm_addr + off);
        if (ptr && *ptr >= 0x10000 && *ptr <= 0x7fffffffffff)
          continue; // It's a pointer

        if (*v > 100 && *v < 1000000000LL)
        {
          size_t uid = G_DUMPER.get_offset("DataModel", "UniverseId").value_or(-1);
          size_t pid = G_DUMPER.get_offset("DataModel", "PrivateServerId").value_or(-1);
          if (off == uid || off == pid)
            continue;
          G_DUMPER.add_offset("DataModel", "PrivateServerOwnerId", off);
          std::cerr << "  DataModel::PrivateServerOwnerId at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read<uint32_t>(fd, dm_addr + off))
      {
        if (*v > 0 && *v < 100)
        {
          uint32_t prev = memory::read<uint32_t>(fd, dm_addr + off - 4).value_or(0xFFFFFFFF);
          uint32_t next = memory::read<uint32_t>(fd, dm_addr + off + 4).value_or(0xFFFFFFFF);
          if (prev > 100 && next > 100)
          {
            G_DUMPER.add_offset("DataModel", "SavaVersion", off);
            std::cerr << "  DataModel::SavaVersion at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }

    struct ServiceInfo
    {
      const char *name;
      const char *ns;
    };

    ServiceInfo services[] = {
      { "RunService@RBX", "RunService" },
      { "UserInputService@RBX", "UserInputService" },
      { "HttpService@RBX", "HttpService" },
      { "MarketplaceService@RBX", "MarketplaceService" },
      { "TeleportService@RBX", "TeleportService" },
      { "SocialService@RBX", "SocialService" },
      { "Chat@RBX", "Chat" },
      { "BadgeService@RBX", "BadgeService" },
      { "InsertService@RBX", "InsertService" },
      { "ScriptContext@RBX", "ScriptContext" },
      { "ContentProvider@RBX", "ContentProvider" },
      { "CorePackages@RBX", "CorePackages" },
    };

    for (const auto &svc : services)
    {
      if (auto off = rtti::find(fd, dm_addr, svc.name, 0x2000, 8))
      {
        G_DUMPER.add_offset(svc.ns, "Service", *off);
        std::cerr << "  " << svc.ns << " at +0x" << std::hex << *off << std::dec << "\n";

        if (std::string(svc.ns) == "RunService")
        {
          for (size_t so_off = 0; so_off < 0x100; so_off += 4)
          {
            if (auto v = memory::read_f32(fd, dm_addr + *off + so_off))
            {
              if (*v > 0.0f && *v < 1.0f)
              {
                G_DUMPER.add_offset("RunService", "SimulationRate", so_off);
                break;
              }
            }
          }
        }
      }
    }
  }
} // namespace stages
