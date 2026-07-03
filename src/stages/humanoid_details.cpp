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

  static std::vector<size_t> find_humanoids(int fd, size_t addr, size_t cs, size_t ce)
  {
    std::vector<size_t> humans;
    if (cs > 0 && ce > 0)
    {
      auto kids = collect_children(fd, addr, cs, ce);
      for (size_t child : kids)
      {
        if (auto r = rtti::scan_rtti(fd, child))
        {
          if (r->name == "Humanoid@RBX")
          {
            humans.push_back(child);
            continue;
          }
        }
        auto grandkids = collect_children(fd, child, cs, ce);
        for (size_t gk : grandkids)
        {
          if (auto r = rtti::scan_rtti(fd, gk))
          {
            if (r->name == "Humanoid@RBX")
              humans.push_back(gk);
          }
        }
      }
    }
    if (humans.empty())
    {
      for (size_t off = 0; off < 0x4000; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, addr + off))
        {
          if (*ptr >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *ptr))
            {
              if (r->name == "Humanoid@RBX")
              {
                humans.push_back(*ptr);
                if (humans.size() >= 3)
                  break;
              }
            }
          }
        }
      }
    }
    return humans;
  }

  void humanoid_details(int fd)
  {
    std::cerr << "[humanoid_details]\n";
    size_t ws_addr = G_WORKSPACE_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    auto humanoids = find_humanoids(fd, ws_addr, cs, ce);
    if (humanoids.empty())
    {
      std::cerr << "  No humanoids found\n";
      return;
    }
    std::cerr << "  Found " << humanoids.size() << " humanoid(s)\n";

    auto dump_hip_height = [&]()
    {
      for (size_t off = 0; off < 0x400; off += 4)
      {
        int valid = 0;
        bool varied = false;
        float first_val = 0.0f;
        for (size_t i = 0; i < humanoids.size(); ++i)
        {
          if (auto v = memory::read_f32(fd, humanoids[i] + off))
          {
            if (std::isnan(*v) || std::isinf(*v) || *v < 0.0f || *v > 100.0f)
              break;
            if (i == 0)
              first_val = *v;
            if (*v > 0.5f && *v < 10.0f)
              valid++;
            if (i > 0 && std::abs(*v - first_val) > 0.1f)
              varied = true;
          }
          else
            break;
        }
        if (valid >= 2 && varied)
        {
          G_DUMPER.add_offset("Humanoid", "HipHeight", off);
          std::cerr << "  Humanoid::HipHeight at +0x" << std::hex << off << std::dec << "\n";
          return;
        }
      }
      for (size_t off = 0; off < 0x400; off += 4)
      {
        if (auto v = memory::read_f32(fd, humanoids[0] + off))
        {
          if (*v > 1.0f && *v < 5.0f && !std::isnan(*v))
          {
            size_t ws = G_DUMPER.get_offset("Humanoid", "WalkSpeed").value_or(-1);
            size_t jp = G_DUMPER.get_offset("Humanoid", "JumpPower").value_or(-1);
            size_t jh = G_DUMPER.get_offset("Humanoid", "JumpHeight").value_or(-1);
            if (off != ws && off != jp && off != jh)
            {
              G_DUMPER.add_offset("Humanoid", "HipHeight", off);
              std::cerr << "  Humanoid::HipHeight at +0x" << std::hex << off << " (fallback)\n" << std::dec;
              return;
            }
          }
        }
      }
    };

    auto dump_humanoid_root_part = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, humanoids[0] + off))
        {
          if (*ptr >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *ptr))
            {
              if (r->name == "Primitive@RBX")
              {
                G_DUMPER.add_offset("Humanoid", "HumanoidRootPart", off);
                std::cerr << "  Humanoid::HumanoidRootPart at +0x" << std::hex << off << std::dec << "\n";
                return;
              }
            }
            if (size_t po = G_DUMPER.get_offset("BasePart", "Primitive").value_or(0))
            {
              if (auto prim = memory::read<size_t>(fd, *ptr + po))
              {
                if (*prim >= 0x10000)
                {
                  if (auto r = rtti::scan_rtti(fd, *prim))
                  {
                    if (r->name == "Primitive@RBX")
                    {
                      G_DUMPER.add_offset("Humanoid", "HumanoidRootPart", off);
                      std::cerr << "  Humanoid::HumanoidRootPart at +0x" << std::hex << off << " (via Primitive)\n" << std::dec;
                      return;
                    }
                  }
                }
              }
            }
          }
        }
      }
    };

    auto dump_rig_type = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 4)
      {
        if (auto v = memory::read<uint32_t>(fd, humanoids[0] + off))
        {
          if (*v <= 2)
          {
            auto next = memory::read<uint32_t>(fd, humanoids[0] + off + 4).value_or(99);
            if (next > 10)
            {
              G_DUMPER.add_offset("Humanoid", "RigType", off);
              std::cerr << "  Humanoid::RigType at +0x" << std::hex << off << std::dec << "\n";
              return;
            }
          }
        }
      }
      for (size_t off = 0; off < 0x200; off += 1)
      {
        if (auto v = memory::read<uint8_t>(fd, humanoids[0] + off))
        {
          if (*v <= 2)
          {
            if (auto v4 = memory::read<uint32_t>(fd, humanoids[0] + off - (off % 4)))
            {
              if ((*v4 & 0xFF) == *v && *v4 < 0x1000)
              {
                G_DUMPER.add_offset("Humanoid", "RigType", off);
                std::cerr << "  Humanoid::RigType at +0x" << std::hex << off << " (u8)\n" << std::dec;
                return;
              }
            }
          }
        }
      }
    };

    auto dump_auto_rotate = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 1)
      {
        bool all_one = true;
        for (size_t h : humanoids)
        {
          auto v = memory::read<uint8_t>(fd, h + off);
          if (!v || *v != 1)
          {
            all_one = false;
            break;
          }
        }
        if (all_one && humanoids.size() >= 2)
        {
          G_DUMPER.add_offset("Humanoid", "AutoRotate", off);
          std::cerr << "  Humanoid::AutoRotate at +0x" << std::hex << off << std::dec << "\n";
          return;
        }
      }
    };

    auto dump_platform_stand = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 1)
      {
        bool all_zero = true;
        for (size_t h : humanoids)
        {
          auto v = memory::read<uint8_t>(fd, h + off);
          if (!v || *v != 0)
          {
            all_zero = false;
            break;
          }
        }
        if (all_zero && humanoids.size() >= 2)
        {
          G_DUMPER.add_offset("Humanoid", "PlatformStand", off);
          std::cerr << "  Humanoid::PlatformStand at +0x" << std::hex << off << std::dec << "\n";
          return;
        }
      }
    };

    auto dump_seat_part = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 8)
      {
        auto ptr = memory::read<size_t>(fd, humanoids[0] + off);
        if (ptr && *ptr == 0)
        {
          bool all_null = true;
          for (size_t i = 1; i < humanoids.size(); ++i)
          {
            if (auto v = memory::read<size_t>(fd, humanoids[i] + off))
            {
              if (*v != 0)
              {
                all_null = false;
                break;
              }
            }
          }
          if (all_null)
          {
            G_DUMPER.add_offset("Humanoid", "SeatPart", off);
            std::cerr << "  Humanoid::SeatPart at +0x" << std::hex << off << " (null)\n" << std::dec;
            return;
          }
        }
        else if (ptr && *ptr >= 0x10000)
        {
          if (auto r = rtti::scan_rtti(fd, *ptr))
          {
            if (r->name == "Primitive@RBX" || r->name.find("Part") != std::string::npos || r->name.find("Seat") != std::string::npos)
            {
              G_DUMPER.add_offset("Humanoid", "SeatPart", off);
              std::cerr << "  Humanoid::SeatPart at +0x" << std::hex << off << std::dec << "\n";
              return;
            }
          }
          if (size_t po = G_DUMPER.get_offset("BasePart", "Primitive").value_or(0))
          {
            if (auto prim = memory::read<size_t>(fd, *ptr + po))
            {
              if (*prim >= 0x10000)
              {
                if (auto r = rtti::scan_rtti(fd, *prim))
                {
                  if (r->name == "Primitive@RBX")
                  {
                    G_DUMPER.add_offset("Humanoid", "SeatPart", off);
                    std::cerr << "  Humanoid::SeatPart at +0x" << std::hex << off << " (via Primitive)\n" << std::dec;
                    return;
                  }
                }
              }
            }
          }
        }
      }
    };

    auto dump_display_distance = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 4)
      {
        if (auto v = memory::read<uint32_t>(fd, humanoids[0] + off))
        {
          if (*v <= 2)
          {
            auto next = memory::read<uint32_t>(fd, humanoids[0] + off + 4).value_or(99);
            if (next > 10)
            {
              G_DUMPER.add_offset("Humanoid", "DisplayDistanceType", off);
              std::cerr << "  Humanoid::DisplayDistanceType at +0x" << std::hex << off << std::dec << "\n";
              return;
            }
          }
        }
      }
    };

    auto dump_name_occlusion = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 4)
      {
        if (auto v = memory::read<uint32_t>(fd, humanoids[0] + off))
        {
          if (*v <= 2)
          {
            auto next = memory::read<uint32_t>(fd, humanoids[0] + off + 4).value_or(99);
            if (next > 10)
            {
              G_DUMPER.add_offset("Humanoid", "NameOcclusion", off);
              std::cerr << "  Humanoid::NameOcclusion at +0x" << std::hex << off << std::dec << "\n";
              return;
            }
          }
        }
      }
    };

    auto dump_camera_offset = [&]()
    {
      for (size_t off = 0; off < 0x200; off += 4)
      {
        if (auto v = memory::read<std::array<float, 3>>(fd, humanoids[0] + off))
        {
          if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
            continue;
          if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
            continue;
          if (std::abs((*v)[0]) < 0.1f && std::abs((*v)[2]) < 0.1f && (*v)[1] >= 0.0f && (*v)[1] <= 3.0f)
          {
            G_DUMPER.add_offset("Humanoid", "CameraOffset", off);
            std::cerr << "  Humanoid::CameraOffset at +0x" << std::hex << off << std::dec << "\n";
            return;
          }
        }
      }
    };

    dump_hip_height();
    dump_humanoid_root_part();
    dump_rig_type();
    dump_auto_rotate();
    dump_platform_stand();
    dump_seat_part();
    dump_display_distance();
    dump_name_occlusion();
    dump_camera_offset();
  }
} // namespace stages
