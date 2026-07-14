#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"

#include <cmath>
#include <iostream>

namespace stages
{
  void sky(int fd)
  {
    std::cerr << "[sky]\n";

    size_t dm_addr = G_DATA_MODEL_ADDR;

    auto lighting_off = rtti::find(fd, dm_addr, "Lighting@RBX", 0x2000, 8);
    std::optional<size_t> lighting_opt;
    if (lighting_off)
    {
      lighting_opt = memory::read<size_t>(fd, dm_addr + *lighting_off);
    }

    if (!lighting_opt)
    {
      std::cerr << "  No Lighting found\n";
      return;
    }
    size_t lighting = *lighting_opt;
    std::cerr << "  Lighting @ 0x" << std::hex << lighting << std::dec << "\n";

    if (auto sky_off = rtti::find(fd, lighting, "Sky@RBX", 0x1000, 8))
    {
      if (auto sky = memory::read<size_t>(fd, lighting + *sky_off))
      {
        if (*sky >= 0x10000)
        {
          std::cerr << "  Sky @ 0x" << std::hex << *sky << std::dec << "\n";

          for (size_t off = 0; off < 0x200; off += 8)
          {
            if (auto ptr = memory::read<size_t>(fd, *sky + off))
            {
              if (*ptr >= 0x10000)
              {
                if (auto first = memory::read<size_t>(fd, *ptr))
                {
                  if (*first >= 0x10000)
                  {
                    if (auto r = rtti::scan_rtti(fd, *first))
                    {
                      if (r->name.find("CelestialBody") != std::string::npos || r->name.find("Sun") != std::string::npos)
                      {
                        G_DUMPER.add_offset("Sky", "CelestialBodies", off);
                        std::cerr << "  Sky::CelestialBodies at +0x" << std::hex << off << std::dec << "\n";
                        break;
                      }
                    }
                  }
                }
              }
            }
          }

          for (size_t off = 0; off < 0x100; off += 4)
          {
            if (auto v = memory::read_f32(fd, *sky + off))
            {
              if (*v > 0.0f && *v < 90.0f && std::abs(*v - 25.0f) < 20.0f)
              {
                G_DUMPER.add_offset("Sky", "MoonAngularSize", off);
                std::cerr << "  Sky::MoonAngularSize at +0x" << std::hex << off << std::dec << "\n";
                break;
              }
            }
          }

          for (size_t off = 0; off < 0x100; off += 4)
          {
            if (auto v = memory::read_f32(fd, *sky + off))
            {
              if (*v > 0.0f && *v < 90.0f && std::abs(*v - 14.0f) < 10.0f)
              {
                size_t moon = G_DUMPER.get_offset("Sky", "MoonAngularSize").value_or(-1);
                if (off != moon)
                {
                  G_DUMPER.add_offset("Sky", "SunAngularSize", off);
                  std::cerr << "  Sky::SunAngularSize at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }
          }

          for (size_t off = 0; off < 0x100; off += 2)
          {
            if (auto v = memory::read<uint16_t>(fd, *sky + off))
            {
              if (*v > 100 && *v < 10000)
              {
                if (auto v32 = memory::read<uint32_t>(fd, *sky + off - (off % 4)))
                {
                  if (*v32 < 100000)
                  {
                    G_DUMPER.add_offset("Sky", "StarCount", off);
                    std::cerr << "  Sky::StarCount at +0x" << std::hex << off << std::dec << "\n";
                    break;
                  }
                }
              }
            }
          }

          for (size_t off = 0; off < 0x300; off += 8)
          {
            if (auto ptr = memory::read<size_t>(fd, *sky + off))
            {
              if (*ptr >= 0x10000)
              {
                if (auto s = memory::read_name_fmt(fd, *ptr))
                {
                  if (s->find("skybox") != std::string::npos || s->find("Skybox") != std::string::npos || s->find("rbxasset://") == 0)
                  {
                    const char *name = nullptr;
                    if (!G_DUMPER.get_offset("Sky", "SkyboxUp"))
                      name = "SkyboxUp";
                    else if (!G_DUMPER.get_offset("Sky", "SkyboxDown"))
                      name = "SkyboxDown";
                    else if (!G_DUMPER.get_offset("Sky", "SkyboxLeft"))
                      name = "SkyboxLeft";
                    else if (!G_DUMPER.get_offset("Sky", "SkyboxRight"))
                      name = "SkyboxRight";
                    else if (!G_DUMPER.get_offset("Sky", "SkyboxFront"))
                      name = "SkyboxFront";
                    else if (!G_DUMPER.get_offset("Sky", "SkyboxBack"))
                      name = "SkyboxBack";
                    else
                      continue;

                    G_DUMPER.add_offset("Sky", name, off);
                    std::cerr << "  Sky::" << name << " at +0x" << std::hex << off << std::dec << "\n";
                    if (G_DUMPER.get_offset("Sky", "SkyboxBack"))
                      break;
                  }
                }
              }
            }
          }
        }
      }
    }

    if (auto sr_off = rtti::find(fd, lighting, "SunRaysEffect@RBX", 0x1000, 8))
    {
      if (auto sr = memory::read<size_t>(fd, lighting + *sr_off))
      {
        if (*sr >= 0x10000)
        {
          std::cerr << "  SunRaysEffect @ 0x" << std::hex << *sr << std::dec << "\n";
          for (size_t off = 0; off < 0x100; off += 4)
          {
            if (auto v = memory::read_f32(fd, *sr + off))
            {
              if (*v >= 0.0f && *v <= 1.0f)
              {
                if (!G_DUMPER.get_offset("SunRaysEffect", "Intensity"))
                {
                  G_DUMPER.add_offset("SunRaysEffect", "Intensity", off);
                }
                else if (!G_DUMPER.get_offset("SunRaysEffect", "Spread"))
                {
                  G_DUMPER.add_offset("SunRaysEffect", "Spread", off);
                  break;
                }
              }
            }
          }
        }
      }
    }

    if (auto c_off = rtti::find(fd, lighting, "Clouds@RBX", 0x2000, 8))
    {
      if (auto c = memory::read<size_t>(fd, lighting + *c_off))
      {
        if (*c >= 0x10000)
        {
          std::cerr << "  Clouds @ 0x" << std::hex << *c << std::dec << "\n";
          for (size_t off = 0; off < 0x100; off += 4)
          {
            if (auto v = memory::read_f32(fd, *c + off))
            {
              if (std::abs(*v - 0.5f) < 0.1f && *v >= 0.0f && *v <= 1.0f)
              {
                G_DUMPER.add_offset("Clouds", "Cover", off);
                std::cerr << "  Clouds::Cover at +0x" << std::hex << off << std::dec << "\n";
                break;
              }
            }
          }
          for (size_t off = 0; off < 0x100; off += 4)
          {
            if (auto v = memory::read_f32(fd, *c + off))
            {
              if (std::abs(*v - 0.5f) < 0.1f && *v >= 0.0f && *v <= 1.0f)
              {
                size_t cov = G_DUMPER.get_offset("Clouds", "Cover").value_or(-1);
                if (off != cov)
                {
                  G_DUMPER.add_offset("Clouds", "Density", off);
                  std::cerr << "  Clouds::Density at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }
          }
        }
      }
    }
  }
} // namespace stages
