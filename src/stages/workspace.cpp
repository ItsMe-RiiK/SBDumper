#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <cmath>
#include <iostream>

namespace stages
{
  bool workspace(int fd)
  {
    std::cerr << "[workspace]\n";

    auto ws_off = G_DUMPER.get_offset("DataModel", "Workspace");
    if (!ws_off)
    {
      std::cerr << "No Workspace offset in DataModel\n";
      return false;
    }

    auto ws_addr = memory::read<size_t>(fd, G_DATA_MODEL_ADDR + *ws_off);
    if (!ws_addr)
    {
      std::cerr << "Failed to read Workspace addr\n";
      return false;
    }
    std::cerr << "  Workspace @ 0x" << std::hex << *ws_addr << std::dec << "\n";
    G_WORKSPACE_ADDR = *ws_addr;

    if (auto cc = rtti::find(fd, *ws_addr, "Camera@RBX", 0x1000, 8))
    {
      G_DUMPER.add_offset("Workspace", "CurrentCamera", *cc);
    }

    if (auto world_off = rtti::find(fd, *ws_addr, "World@RBX", 0x1000, 8))
    {
      G_DUMPER.add_offset("Workspace", "World", *world_off);
      std::cerr << "  World at +0x" << std::hex << *world_off << std::dec << "\n";

      if (auto world_addr = memory::read<size_t>(fd, *ws_addr + *world_off))
      {
        if (*world_addr >= 0x10000)
        {
          for (size_t off = 0; off < 0x300; off += 4)
          {
            auto v = memory::read<std::array<float, 3>>(fd, *world_addr + off);
            if (!v)
              continue;

            bool invalid = false;
            for (float x : *v)
            {
              if (std::isnan(x) || std::isinf(x) || std::fpclassify(x) == FP_SUBNORMAL)
              {
                invalid = true;
                break;
              }
            }
            if (invalid)
              continue;

            if (std::abs((*v)[0]) < 0.01f && std::abs((*v)[2]) < 0.01f && (*v)[1] < 0.0f && (*v)[1] > -2000.0f)
            {
              G_DUMPER.add_offset("World", "Gravity", off);
              std::cerr << "  World::Gravity at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }

          for (size_t off = 0; off < 0x800; off += 8)
          {
            auto ptr = memory::read<size_t>(fd, *world_addr + off);
            if (!ptr || *ptr < 0x10000)
              continue;

            auto first_slot = memory::read<size_t>(fd, *ptr);
            if (!first_slot || *first_slot < 0x10000)
              continue;

            if (auto r = rtti::scan_rtti(fd, *first_slot))
            {
              if (r->name == "Primitive@RBX")
              {
                G_DUMPER.add_offset("World", "Primitives", off);
                std::cerr << "  World::Primitives at +0x" << std::hex << off << std::dec << "\n";
                break;
              }
            }
          }
        }
      }
    }

    return true;
  }
} // namespace stages
