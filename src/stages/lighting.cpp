#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <cmath>
#include <iostream>
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

  void lighting(int fd)
  {
    std::cerr << "[lighting]\n";

    size_t dm_addr = G_DATA_MODEL_ADDR;
    size_t ws_addr = G_WORKSPACE_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    std::optional<size_t> lighting_addr;
    if (auto off = rtti::find(fd, dm_addr, "Lighting@RBX", 0x1000, 8))
    {
      lighting_addr = memory::read<size_t>(fd, dm_addr + *off);
    }
    else if (cs > 0 && ce > 0)
    {
      auto kids = collect_children(fd, dm_addr, cs, ce);
      for (size_t c : kids)
      {
        if (auto r = rtti::scan_rtti(fd, c))
        {
          if (r->name == "Lighting@RBX")
          {
            lighting_addr = c;
            break;
          }
        }
      }
    }

    if (lighting_addr)
    {
      size_t lighting = *lighting_addr;
      std::cerr << "  Lighting @ 0x" << std::hex << lighting << std::dec << "\n";

      for (size_t off = 0; off < 0x200; off += 4)
      {
        if (auto v = memory::read_f32(fd, lighting + off))
        {
          if (std::abs(*v - 1.0f) < 0.1f && *v > 0.0f && *v < 10.0f)
          {
            G_DUMPER.add_offset("Lighting", "Brightness", off);
            std::cerr << "  Lighting::Brightness at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }

      if (auto atmo_off = rtti::find(fd, lighting, "Atmosphere@RBX", 0x1000, 8))
      {
        if (auto atmo = memory::read<size_t>(fd, lighting + *atmo_off))
        {
          if (*atmo >= 0x10000)
          {
            std::cerr << "  Atmosphere @ 0x" << std::hex << *atmo << std::dec << "\n";
            for (size_t off = 0; off < 0x100; off += 4)
            {
              if (auto v = memory::read<std::array<float, 3>>(fd, *atmo + off))
              {
                bool bad = false;
                float sum = 0.0f;
                for (float x : *v)
                {
                  if (std::isnan(x) || std::isinf(x))
                    bad = true;
                  else if (x < 0.0f || x > 1.0f)
                    bad = true;
                  sum += x;
                }
                if (!bad && sum > 0.1f)
                {
                  G_DUMPER.add_offset("Atmosphere", "Color", off);
                  std::cerr << "  Atmosphere::Color at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }

            const char *names[] = {"Decay", "Glare", "Density", "Haze", "Offset"};
            for (const char *name : names)
            {
              std::vector<size_t> skip_offs;
              if (auto c = G_DUMPER.get_offset("Atmosphere", "Color"))
              {
                skip_offs.push_back(*c);
                skip_offs.push_back(*c + 4);
                skip_offs.push_back(*c + 8);
              }
              for (const char *prev : names)
              {
                if (std::string(prev) == name)
                  break;
                if (auto o = G_DUMPER.get_offset("Atmosphere", prev))
                {
                  skip_offs.push_back(*o);
                }
              }
              for (size_t off = 0; off < 0x100; off += 4)
              {
                if (auto v = memory::read_f32(fd, *atmo + off))
                {
                  if (*v >= 0.0f && *v <= 1.0f)
                  {
                    bool skip = false;
                    for (size_t so : skip_offs)
                      if (off == so)
                        skip = true;
                    if (!skip)
                    {
                      G_DUMPER.add_offset("Atmosphere", name, off);
                      std::cerr << "  Atmosphere::" << name << " at +0x" << std::hex << off << std::dec << "\n";
                      break;
                    }
                  }
                }
              }
            }
          }
        }
      }

      if (auto bloom_off = rtti::find(fd, lighting, "BloomEffect@RBX", 0x1000, 8))
      {
        if (auto bloom = memory::read<size_t>(fd, lighting + *bloom_off))
        {
          if (*bloom >= 0x10000)
          {
            std::cerr << "  BloomEffect @ 0x" << std::hex << *bloom << std::dec << "\n";
            for (size_t off = 0; off < 0x200; off += 4)
            {
              if (auto v = memory::read_f32(fd, *bloom + off))
              {
                if (*v >= 0.0f && *v < 5.0f)
                {
                  G_DUMPER.add_offset("BloomEffect", "Threshold", off);
                  std::cerr << "  BloomEffect::Threshold at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }
          }
        }
      }
    }

    std::optional<size_t> terrain_addr;
    if (auto off = rtti::find(fd, ws_addr, "Terrain@RBX", 0x1000, 8))
    {
      terrain_addr = memory::read<size_t>(fd, ws_addr + *off);
    }
    else if (cs > 0 && ce > 0)
    {
      auto kids = collect_children(fd, ws_addr, cs, ce);
      for (size_t c : kids)
      {
        if (auto r = rtti::scan_rtti(fd, c))
        {
          if (r->name == "Terrain@RBX")
          {
            terrain_addr = c;
            break;
          }
        }
      }
    }

    if (terrain_addr)
    {
      size_t terrain = *terrain_addr;
      std::cerr << "  Terrain @ 0x" << std::hex << terrain << std::dec << "\n";

      for (size_t off = 0; off < 0x200; off += 4)
      {
        if (auto v = memory::read<std::array<float, 3>>(fd, terrain + off))
        {
          bool bad = false;
          float sum = 0.0f;
          for (float x : *v)
          {
            if (std::isnan(x) || std::isinf(x) || x < 0.0f || x > 1.0f)
              bad = true;
            sum += x;
          }
          if (!bad && sum > 0.1f)
          {
            G_DUMPER.add_offset("Terrain", "WaterColor", off);
            std::cerr << "  Terrain::WaterColor at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }

      const char *tnames[] = {"WaterReflectance", "WaterTransparency", "GrassLength", "WaterWaveSize", "WaterWaveSpeed"};
      for (const char *name : tnames)
      {
        std::vector<size_t> skip_offs;
        if (auto c = G_DUMPER.get_offset("Terrain", "WaterColor"))
        {
          skip_offs.push_back(*c);
          skip_offs.push_back(*c + 4);
          skip_offs.push_back(*c + 8);
        }
        for (const char *prev : tnames)
        {
          if (std::string(prev) == name)
            break;
          if (auto o = G_DUMPER.get_offset("Terrain", prev))
            skip_offs.push_back(*o);
        }
        for (size_t off = 0; off < 0x200; off += 4)
        {
          if (auto v = memory::read_f32(fd, terrain + off))
          {
            if (*v >= 0.0f && *v <= 1.0f)
            {
              bool skip = false;
              for (size_t so : skip_offs)
                if (off == so)
                  skip = true;
              if (!skip)
              {
                G_DUMPER.add_offset("Terrain", name, off);
                std::cerr << "  Terrain::" << name << " at +0x" << std::hex << off << std::dec << "\n";
                break;
              }
            }
          }
        }
      }

      for (size_t off = 0; off < 0x600; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, terrain + off))
        {
          if (*ptr < 0x10000)
            continue;
          if (auto c1 = memory::read<std::array<float, 3>>(fd, *ptr))
          {
            bool bad = false;
            float sum = 0.0f;
            for (float x : *c1)
            {
              if (std::isnan(x) || std::isinf(x) || x < 0.0f || x > 1.0f)
                bad = true;
              sum += x;
            }
            if (!bad && sum > 0.01f)
            {
              if (memory::read<std::array<float, 3>>(fd, *ptr + 0xC) && memory::read<std::array<float, 3>>(fd, *ptr + 0x18))
              {
                G_DUMPER.add_offset("Terrain", "MaterialColors", off);
                std::cerr << "  Terrain::MaterialColors at +0x" << std::hex << off << std::dec << "\n";
                break;
              }
            }
          }
        }
      }
    }
  }
} // namespace stages
