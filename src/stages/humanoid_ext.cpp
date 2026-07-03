#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <cmath>
#include <iostream>
#include <vector>

namespace stages
{
  void humanoid_ext(int fd)
  {
    std::cerr << "[humanoid_ext]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);
    size_t walk_off = G_DUMPER.get_offset("Humanoid", "WalkSpeed").value_or(0);

    std::vector<size_t> humans;
    for (size_t off = 0; off < 0x4000; off += 8)
    {
      if (auto ptr = memory::read<size_t>(fd, ws_addr + off))
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
    if (humans.empty() && cs > 0 && ce > 0)
    {
      if (auto head = memory::read<size_t>(fd, ws_addr + cs))
      {
        if (*head >= 0x10000)
        {
          size_t first = memory::read<size_t>(fd, *head).value_or(0);
          size_t last = memory::read<size_t>(fd, *head + ce).value_or(0);
          if (first >= 0x10000 && last >= 0x10000)
          {
            size_t node = first;
            while (node != last && node != 0)
            {
              if (auto child = memory::read<size_t>(fd, node))
              {
                if (*child >= 0x10000)
                {
                  if (auto r = rtti::scan_rtti(fd, *child))
                  {
                    if (r->name == "Humanoid@RBX")
                      humans.push_back(*child);
                  }

                  size_t h = memory::read<size_t>(fd, *child + cs).value_or(0);
                  size_t f = h >= 0x10000 ? memory::read<size_t>(fd, h).value_or(0) : 0;
                  size_t l = h >= 0x10000 ? memory::read<size_t>(fd, h + ce).value_or(0) : 0;
                  std::vector<size_t> gv;
                  if (f >= 0x10000 && l >= 0x10000)
                  {
                    size_t n = f;
                    while (n != l && n != 0)
                    {
                      if (auto gc = memory::read<size_t>(fd, n))
                      {
                        if (*gc >= 0x10000)
                          gv.push_back(*gc);
                      }
                      n += 0x10;
                    }
                  }
                  for (size_t gk : gv)
                  {
                    if (auto r = rtti::scan_rtti(fd, gk))
                    {
                      if (r->name == "Humanoid@RBX")
                        humans.push_back(gk);
                    }
                  }
                }
              }
              node += 0x10;
              if (humans.size() >= 3)
                break;
            }
          }
        }
      }
    }

    if (humans.empty())
    {
      std::cerr << "  No Humanoid found for ext\n";
      return;
    }
    std::cerr << "  Extended scan on " << humans.size() << " Humanoid(s)\n";

    size_t h = humans[0];

    for (size_t off = 0; off < 0x400; off += 4)
    {
      if (off == walk_off || off == walk_off + 4)
        continue;
      if (auto v = memory::read_f32(fd, h + off))
      {
        if (*v >= 0.0f && *v <= 5.0f && !std::isnan(*v) && !std::isinf(*v) && std::isnormal(*v) || *v == 0.0f)
        {
          bool skip = false;
          for (size_t i = 1; i < humans.size(); ++i)
          {
            if (auto ov = memory::read_f32(fd, humans[i] + off))
            {
              if (std::abs(*ov - *v) > 0.1f)
              {
                skip = true;
                break;
              }
            }
          }
          if (humans.size() <= 1 || !skip)
          {
            G_DUMPER.add_offset("Humanoid", "HipHeight", off);
            std::cerr << "  Humanoid::HipHeight at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, h + off))
      {
        if (*v <= 1)
        {
          bool all_same = true;
          for (size_t i = 1; i < humans.size(); ++i)
          {
            if (auto ov = memory::read<uint8_t>(fd, humans[i] + off))
            {
              if (*ov != *v)
              {
                all_same = false;
                break;
              }
            }
          }
          uint8_t prev = memory::read<uint8_t>(fd, h + off - 1).value_or(2);
          uint8_t next = memory::read<uint8_t>(fd, h + off + 1).value_or(2);
          if (prev > 1 && next > 1 && (humans.size() <= 1 || !all_same))
          {
            G_DUMPER.add_offset("Humanoid", "RigType", off);
            std::cerr << "  Humanoid::RigType at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, h + off))
      {
        if (*v == 0)
        {
          size_t hip_height = G_DUMPER.get_offset("Humanoid", "HipHeight").value_or(-1);
          bool near_hip = (hip_height != (size_t)-1) && (off >= hip_height && off <= hip_height + 3);
          if (!near_hip)
          {
            uint8_t prev = memory::read<uint8_t>(fd, h + off - 1).value_or(2);
            uint8_t next = memory::read<uint8_t>(fd, h + off + 1).value_or(2);
            if (prev > 1 && next > 1)
            {
              G_DUMPER.add_offset("Humanoid", "Sit", off);
              std::cerr << "  Humanoid::Sit at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, h + off))
      {
        if (*v >= 1 && *v <= 40)
        {
          size_t rig = G_DUMPER.get_offset("Humanoid", "RigType").value_or(-1);
          size_t sit = G_DUMPER.get_offset("Humanoid", "Sit").value_or(-1);
          bool near_rig = (rig != (size_t)-1) && (off == rig || off == rig + 1);
          bool near_sit = (sit != (size_t)-1) && (off == sit || off == sit + 1);
          if (near_rig || near_sit)
            continue;

          uint8_t prev = memory::read<uint8_t>(fd, h + off - 1).value_or(0);
          uint8_t next = memory::read<uint8_t>(fd, h + off + 1).value_or(0);
          if (prev > 40 && next > 40)
          {
            G_DUMPER.add_offset("Humanoid", "FloorMaterial", off);
            std::cerr << "  Humanoid::FloorMaterial at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, h + off))
      {
        if (*v == 1)
        {
          size_t sit = G_DUMPER.get_offset("Humanoid", "Sit").value_or(-1);
          size_t rig = G_DUMPER.get_offset("Humanoid", "RigType").value_or(-1);
          size_t floor = G_DUMPER.get_offset("Humanoid", "FloorMaterial").value_or(-1);
          if (off == sit || off == rig || off == floor)
            continue;

          uint8_t prev = memory::read<uint8_t>(fd, h + off - 1).value_or(2);
          uint8_t next = memory::read<uint8_t>(fd, h + off + 1).value_or(2);
          if (prev > 1 && next > 1)
          {
            G_DUMPER.add_offset("Humanoid", "AutoRotate", off);
            std::cerr << "  Humanoid::AutoRotate at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, h + off))
      {
        if (*v == 1)
        {
          std::vector<size_t> skip = {
              G_DUMPER.get_offset("Humanoid", "Sit").value_or(-1), G_DUMPER.get_offset("Humanoid", "RigType").value_or(-1),
              G_DUMPER.get_offset("Humanoid", "AutoRotate").value_or(-1), G_DUMPER.get_offset("Humanoid", "FloorMaterial").value_or(-1)};
          bool in_skip = false;
          for (size_t s : skip)
            if (s == off)
              in_skip = true;
          if (in_skip)
            continue;

          uint8_t prev = memory::read<uint8_t>(fd, h + off - 1).value_or(2);
          uint8_t next = memory::read<uint8_t>(fd, h + off + 1).value_or(2);
          if (prev > 1 && next > 1)
          {
            G_DUMPER.add_offset("Humanoid", "UseJumpPower", off);
            std::cerr << "  Humanoid::UseJumpPower at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }
  }
} // namespace stages
