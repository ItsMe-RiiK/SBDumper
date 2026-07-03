#include "dumper.hpp"
#include "memory.hpp"
#include "process.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <cmath>
#include <iostream>

namespace stages
{
  bool visual_engine(const Process &proc, int fd)
  {
    std::cerr << "[visual_engine]\n";

    const char *sections[] = {".data", ".rdata", ".rodata"};
    const char *targets[] = {"VisualEngine@Graphics@RBX", "DataModel@RBX"};

    std::optional<size_t> ve_off;
    std::optional<size_t> dm_off;

    auto on_match = [&](size_t module_off, const std::string &name)
    {
      if (name == targets[0] && !ve_off)
      {
        ve_off = module_off;
        std::cerr << "  VisualEngine ptr at module+0x" << std::hex << module_off << std::dec << "\n";
      }
      if (name == targets[1] && !dm_off)
      {
        dm_off = module_off;
        std::cerr << "  FakeDataModel ptr at module+0x" << std::hex << module_off << std::dec << "\n";
      }
    };

    for (const char *sn : sections)
    {
      auto s_opt = proc.get_section(sn);
      if (!s_opt)
        continue;
      auto [start, size] = *s_opt;
      rtti::scan_section_batched(fd, start, size, proc.get_module_base(), 8, on_match);
      if (ve_off && dm_off)
        break;
    }

    if (!ve_off)
    {
      std::cerr << "VisualEngine pointer not found\n";
      return false;
    }
    if (!dm_off)
    {
      std::cerr << "FakeDataModel pointer not found\n";
      return false;
    }

    size_t ve_ptr_off = *ve_off;
    size_t dm_ptr_off = *dm_off;

    G_DUMPER.add_offset("VisualEngine", "Pointer", ve_ptr_off);
    G_DUMPER.add_offset("FakeDataModel", "Pointer", dm_ptr_off);

    auto ve_opt = memory::read<size_t>(fd, proc.get_module_base() + ve_ptr_off);
    if (!ve_opt)
    {
      std::cerr << "Failed to read VisualEngine\n";
      return false;
    }
    size_t ve = *ve_opt;
    std::cerr << "  VisualEngine @ 0x" << std::hex << ve << std::dec << "\n";

    if (auto rv_off = rtti::find(fd, ve, "RenderView@Graphics@RBX", 0x1000, 8))
    {
      G_DUMPER.add_offset("VisualEngine", "RenderView", *rv_off);
      if (auto rv = memory::read<size_t>(fd, ve + *rv_off))
      {
        for (size_t off = 0; off < 0x300; off += 2)
        {
          if (auto v = memory::read<uint16_t>(fd, *rv + off))
          {
            if (*v == 257)
            {
              G_DUMPER.add_offset("RenderView", "LightingValid", off);
              break;
            }
          }
        }
        G_DUMPER.add_offset("RenderView", "SkyboxValid", 0x28d);
      }
    }

    for (size_t off = 0; off < 0x2000; off += 0x10)
    {
      float m[16] = {0};
      bool ok = true;
      for (int i = 0; i < 16; ++i)
      {
        if (auto v = memory::read_f32(fd, ve + off + i * 4))
        {
          m[i] = *v;
        }
        else
        {
          ok = false;
          break;
        }
      }
      if (!ok)
        continue;
      if (std::abs(m[11] - 0.1f) > 0.01f)
        continue;
      if (std::abs(m[14] + 1.0f) < 0.01f && std::abs(m[15]) < 0.01f)
        continue;
      if (std::abs(m[15]) < 10.0f || std::abs(m[15]) > 10000.0f)
        continue;

      bool has_nan = false;
      for (float v : m)
      {
        if (std::isnan(v) || std::isinf(v))
        {
          has_nan = true;
          break;
        }
      }
      if (has_nan)
        continue;

      G_DUMPER.add_offset("VisualEngine", "ViewMatrix", off);
      break;
    }

    auto fdm_off_opt = rtti::find(fd, ve, "DataModel@RBX", 0x1000, 8);
    if (!fdm_off_opt)
    {
      std::cerr << "FakeDataModel not found in VisualEngine\n";
      return false;
    }
    G_DUMPER.add_offset("VisualEngine", "FakeDataModel", *fdm_off_opt);

    for (size_t off = 0x3EB8000; off < 0x3EB9000; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, proc.get_module_base() + off))
      {
        if (*v == 0xC3)
        {
          G_DUMPER.add_offset("Print", "Print", off - 1);
          std::cerr << "  Print at module+0x" << std::hex << (off - 1) << std::dec << "\n";
          break;
        }
        if (*v == 0xCC)
        {
          G_DUMPER.add_offset("Print", "Print", off);
          std::cerr << "  Print at module+0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }
    if (!G_DUMPER.get_offset("Print", "Print"))
    {
      G_DUMPER.add_offset("Print", "Print", 0x3EB8648);
    }

    for (size_t off = 0x71C9500; off < 0x71C9600; off += 8)
    {
      if (auto ptr = memory::read<size_t>(fd, proc.get_module_base() + off))
      {
        if (*ptr >= 0x10000 && *ptr <= 0x7fffffffffff)
        {
          G_DUMPER.add_offset("StatsItem", "ServicePtr", off);
          std::cerr << "  StatsItem::ServicePtr at module+0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }
    if (!G_DUMPER.get_offset("StatsItem", "ServicePtr"))
    {
      G_DUMPER.add_offset("StatsItem", "ServicePtr", 0x71C9558);
    }

    G_VISUAL_ENGINE = ve;
    return true;
  }
} // namespace stages
