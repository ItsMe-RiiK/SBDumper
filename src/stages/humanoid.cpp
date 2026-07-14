#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"

#include <algorithm>
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

  static std::vector<size_t> find_humanoids_direct(int fd, size_t addr)
  {
    std::vector<size_t> out;
    for (size_t off = 0; off < 0x2000; off += 8)
    {
      auto ptr = memory::read<size_t>(fd, addr + off);
      if (!ptr || *ptr < 0x10000)
        continue;
      if (auto r = rtti::scan_rtti(fd, *ptr))
      {
        if (r->name == "Humanoid@RBX")
        {
          out.push_back(*ptr);
          if (out.size() >= 3)
            break;
        }
      }
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
            {
              humans.push_back(gk);
            }
          }
        }
      }
    }
    if (humans.empty())
    {
      humans = find_humanoids_direct(fd, addr);
    }
    return humans;
  }

  void humanoid(int fd)
  {
    std::cerr << "[humanoid]\n";
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

    size_t h = humanoids[0];

    for (size_t off = 0; off < 0x400; off += 4)
    {
      auto v = memory::read_f32(fd, h + off);
      if (!v)
        continue;
      if (std::abs(*v - 16.0f) < 1.0f)
      {
        G_DUMPER.add_offset("Humanoid", "WalkSpeed", off);
        for (size_t off2 = off + 8; off2 < 0x600; off2 += 4)
        {
          if (auto v2 = memory::read_f32(fd, h + off2))
          {
            if (std::abs(*v2 - 16.0f) < 1.0f)
            {
              G_DUMPER.add_offset("Humanoid", "WalkSpeedCheck", off2);
              break;
            }
          }
        }
        break;
      }
    }

    std::vector<size_t> hundred_floats;
    for (size_t off = 0; off < 0x400; off += 4)
    {
      auto v = memory::read_f32(fd, h + off);
      if (!v)
        continue;
      if (std::abs(*v - 100.0f) < 10.0f)
      {
        hundred_floats.push_back(off);
        if (hundred_floats.size() >= 2)
          break;
      }
    }
    if (hundred_floats.size() >= 2)
    {
      std::sort(hundred_floats.begin(), hundred_floats.end());
      G_DUMPER.add_offset("Humanoid", "MaxHealth", hundred_floats[0]);
      G_DUMPER.add_offset("Humanoid", "Health", hundred_floats[1]);
      std::cerr << "  MaxHealth at +0x" << std::hex << hundred_floats[0] << ", Health at +0x" << hundred_floats[1] << std::dec << "\n";
    }
    else if (hundred_floats.size() == 1)
    {
      G_DUMPER.add_offset("Humanoid", "Health", hundred_floats[0]);
      std::cerr << "  Health at +0x" << std::hex << hundred_floats[0] << std::dec << "\n";
    }

    for (size_t off = 0; off < 0x400; off += 4)
    {
      auto v = memory::read_f32(fd, h + off);
      if (!v)
        continue;
      if (std::abs(*v - 1.8f) < 0.5f && *v > 0.5f && *v < 5.0f)
      {
        bool all_same = true;
        for (size_t i = 1; i < humanoids.size(); ++i)
        {
          if (auto ov = memory::read_f32(fd, humanoids[i] + off))
          {
            if (std::abs(*ov - *v) > 0.1f)
            {
              all_same = false;
              break;
            }
          }
        }
        if (humanoids.size() <= 1 || !all_same)
        {
          G_DUMPER.add_offset("Humanoid", "JumpHeight", off);
          std::cerr << "  JumpHeight at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x400; off += 4)
    {
      auto v = memory::read_f32(fd, h + off);
      if (!v)
        continue;
      if (std::abs(*v - 50.0f) < 5.0f && *v > 10.0f)
      {
        G_DUMPER.add_offset("Humanoid", "JumpPower", off);
        std::cerr << "  JumpPower at +0x" << std::hex << off << std::dec << "\n";
        break;
      }
    }

    for (size_t off = 0; off < 0x400; off += 4)
    {
      auto v = memory::read_f32(fd, h + off);
      if (!v)
        continue;
      if (std::abs(*v - 89.0f) < 10.0f && *v > 30.0f && *v < 100.0f)
      {
        G_DUMPER.add_offset("Humanoid", "MaxSlopeAngle", off);
        std::cerr << "  MaxSlopeAngle at +0x" << std::hex << off << std::dec << "\n";
        break;
      }
    }
  }
} // namespace stages
