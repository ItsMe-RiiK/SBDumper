#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <cmath>
#include <iostream>
#include <vector>

namespace stages
{
  static std::vector<size_t> collect_children(int fd, size_t addr)
  {
    std::vector<size_t> out;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);
    if (cs == 0 || ce == 0)
      return out;

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

  void character_ext(int fd)
  {
    std::cerr << "[character_ext]\n";

    size_t dm_addr = G_DATA_MODEL_ADDR;
    size_t lp_off = G_DUMPER.get_offset("Players", "LocalPlayer").value_or(0);
    if (lp_off == 0)
    {
      std::cerr << "  No LocalPlayer offset\n";
      return;
    }

    std::optional<size_t> pa_opt;
    const size_t ranges[] = {0x2000, 0x4000, 0x8000};
    for (size_t range : ranges)
    {
      if (auto off = rtti::find(fd, dm_addr, "Players@RBX", range, 8))
      {
        if (auto pa = memory::read<size_t>(fd, dm_addr + *off))
        {
          pa_opt = pa;
          break;
        }
      }
    }
    if (!pa_opt)
    {
      for (size_t off = 0; off < 0x2000; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, dm_addr + off))
        {
          if (*ptr >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *ptr))
            {
              if (r->name == "Players@RBX")
              {
                pa_opt = ptr;
                break;
              }
            }
          }
        }
      }
    }
    if (!pa_opt)
    {
      std::cerr << "  Players not found\n";
      return;
    }
    size_t pa = *pa_opt;

    auto lp_addr_opt = memory::read<size_t>(fd, pa + lp_off);
    if (!lp_addr_opt)
    {
      std::cerr << "  LocalPlayer not found\n";
      return;
    }
    size_t lp_addr = *lp_addr_opt;

    size_t char_off = G_DUMPER.get_offset("Player", "Character").value_or(0);
    if (char_off == 0)
      return;

    auto char_addr_opt = memory::read<size_t>(fd, lp_addr + char_off);
    if (!char_addr_opt)
    {
      std::cerr << "  Character not found\n";
      return;
    }
    size_t char_addr = *char_addr_opt;
    std::cerr << "  Character @ 0x" << std::hex << char_addr << std::dec << "\n";

    auto kids = collect_children(fd, char_addr);
    for (size_t child : kids)
    {
      if (auto r = rtti::scan_rtti(fd, child))
      {
        if (r->name == "Tool@RBX")
        {
          std::cerr << "  Tool @ 0x" << std::hex << child << std::dec << "\n";

          for (size_t off = 0; off < 0x200; off += 8)
          {
            if (auto ptr = memory::read<size_t>(fd, child + off))
            {
              if (*ptr >= 0x10000)
              {
                if (auto r2 = rtti::scan_rtti(fd, *ptr))
                {
                  if (r2->name.find("Part") != std::string::npos || r2->name == "BasePart@RBX")
                  {
                    G_DUMPER.add_offset("Tool", "Handle", off);
                    std::cerr << "  Tool::Handle at +0x" << std::hex << off << std::dec << "\n";
                    break;
                  }
                }
              }
            }
          }

          for (size_t off = 0; off < 0x200; off += 8)
          {
            if (auto ptr = memory::read<size_t>(fd, child + off))
            {
              if (*ptr >= 0x10000)
              {
                if (auto s = memory::read_name_fmt(fd, *ptr))
                {
                  if (s->length() >= 2 && s->length() < 100)
                  {
                    bool has_alnum = false;
                    for (char c : *s)
                      if (std::isalnum(c))
                        has_alnum = true;
                    if (has_alnum && !G_DUMPER.get_offset("Tool", "ToolTip"))
                    {
                      G_DUMPER.add_offset("Tool", "ToolTip", off);
                      std::cerr << "  Tool::ToolTip at +0x" << std::hex << off << std::dec << "\n";
                    }
                  }
                }
              }
            }
          }

          for (size_t off = 0; off < 0x100; off += 1)
          {
            if (auto v = memory::read<uint8_t>(fd, child + off))
            {
              if (*v == 1)
              {
                uint8_t prev = memory::read<uint8_t>(fd, child + off - 1).value_or(2);
                uint8_t next = memory::read<uint8_t>(fd, child + off + 1).value_or(2);
                if (prev > 1 && next > 1)
                {
                  G_DUMPER.add_offset("Tool", "CanBeDropped", off);
                  std::cerr << "  Tool::CanBeDropped at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }
          }
        }
      }
    }

    for (size_t child : kids)
    {
      if (auto r = rtti::scan_rtti(fd, child))
      {
        const std::string &typ = r->name;
        if (typ == "BodyVelocity@RBX" || typ == "BodyPosition@RBX" || typ == "BodyGyro@RBX" || typ == "BodyThrust@RBX")
        {
          std::cerr << "  " << typ << " @ 0x" << std::hex << child << std::dec << "\n";

          if (typ == "BodyVelocity@RBX")
          {
            for (size_t off = 0; off < 0x100; off += 4)
            {
              if (auto v = memory::read<std::array<float, 3>>(fd, child + off))
              {
                if (std::abs((*v)[0]) < 10000.0f && std::abs((*v)[1]) < 10000.0f && std::abs((*v)[2]) < 10000.0f)
                {
                  G_DUMPER.add_offset("BodyVelocity", "Velocity", off);
                  std::cerr << "  BodyVelocity::Velocity at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }
            for (size_t off = 0; off < 0x100; off += 4)
            {
              if (auto v = memory::read_f32(fd, child + off))
              {
                if (*v > 0.0f && *v < 100000.0f)
                {
                  auto vel_off = G_DUMPER.get_offset("BodyVelocity", "Velocity");
                  bool near_vel = vel_off && (off >= *vel_off && off <= *vel_off + 8);
                  if (!near_vel)
                  {
                    G_DUMPER.add_offset("BodyVelocity", "P", off);
                    std::cerr << "  BodyVelocity::P at +0x" << std::hex << off << std::dec << "\n";
                    break;
                  }
                }
              }
            }
          }

          if (typ == "BodyPosition@RBX")
          {
            for (size_t off = 0; off < 0x100; off += 4)
            {
              if (auto v = memory::read<std::array<float, 3>>(fd, child + off))
              {
                if (std::abs((*v)[0]) < 100000.0f && std::abs((*v)[1]) < 100000.0f && std::abs((*v)[2]) < 100000.0f)
                {
                  G_DUMPER.add_offset("BodyPosition", "Position", off);
                  std::cerr << "  BodyPosition::Position at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }
            for (size_t off = 0; off < 0x100; off += 4)
            {
              if (auto v = memory::read_f32(fd, child + off))
              {
                if (*v > 0.0f && *v < 100000.0f)
                {
                  auto pos_off = G_DUMPER.get_offset("BodyPosition", "Position");
                  bool near_pos = pos_off && (off >= *pos_off && off <= *pos_off + 8);
                  if (!near_pos)
                  {
                    G_DUMPER.add_offset("BodyPosition", "P", off);
                    std::cerr << "  BodyPosition::P at +0x" << std::hex << off << std::dec << "\n";
                    break;
                  }
                }
              }
            }
            for (size_t off = 0x80; off < 0x200; off += 4)
            {
              if (auto v = memory::read<std::array<float, 3>>(fd, child + off))
              {
                if (std::abs((*v)[0]) > 100.0f && (*v)[0] < 1e12f && std::abs((*v)[1]) > 100.0f && (*v)[1] < 1e12f &&
                    std::abs((*v)[2]) > 100.0f && (*v)[2] < 1e12f)
                {
                  G_DUMPER.add_offset("BodyPosition", "MaxForce", off);
                  std::cerr << "  BodyPosition::MaxForce at +0x" << std::hex << off << std::dec << "\n";
                  break;
                }
              }
            }
          }

          if (typ == "BodyGyro@RBX")
          {
            for (size_t off = 0; off < 0x100; off += 4)
            {
              if (auto buf = memory::read_bytes(fd, child + off, 48))
              {
                const float *f = reinterpret_cast<const float *>(buf->data());
                bool ok = true;
                for (int i = 0; i < 12; ++i)
                  if (std::isnan(f[i]) || std::isinf(f[i]))
                    ok = false;
                if (ok)
                {
                  bool ortho = true;
                  for (int i = 0; i < 3; ++i)
                  {
                    float len = std::sqrt(f[i * 3] * f[i * 3] + f[i * 3 + 1] * f[i * 3 + 1] + f[i * 3 + 2] * f[i * 3 + 2]);
                    if (std::abs(len - 1.0f) > 0.02f)
                    {
                      ortho = false;
                      break;
                    }
                  }
                  if (ortho)
                  {
                    G_DUMPER.add_offset("BodyGyro", "CFrame", off);
                    std::cerr << "  BodyGyro::CFrame at +0x" << std::hex << off << std::dec << "\n";
                    break;
                  }
                }
              }
            }
          }
        }
      }
    }

    for (size_t child : kids)
    {
      if (auto r = rtti::scan_rtti(fd, child))
      {
        if (r->name == "Humanoid@RBX")
        {
          auto hkids = collect_children(fd, child);
          for (size_t hk : hkids)
          {
            if (auto r2 = rtti::scan_rtti(fd, hk))
            {
              if (r2->name == "HumanoidDescription@RBX")
              {
                std::cerr << "  HumanoidDescription @ 0x" << std::hex << hk << std::dec << "\n";

                for (size_t off = 0; off < 0x200; off += 8)
                {
                  if (auto ptr = memory::read<size_t>(fd, hk + off))
                  {
                    if (*ptr >= 0x10000)
                    {
                      float first = memory::read_f32(fd, *ptr).value_or(-1.0f);
                      if (first >= 0.0f && first <= 1.0f)
                      {
                        float second = memory::read_f32(fd, *ptr + 4).value_or(-1.0f);
                        if (second >= 0.0f && second <= 1.0f)
                        {
                          G_DUMPER.add_offset("HumanoidDescription", "BodyProportions", off);
                          std::cerr << "  HumanoidDescription::BodyProportions at +0x" << std::hex << off << std::dec << "\n";
                          break;
                        }
                      }
                    }
                  }
                }

                for (size_t off = 0; off < 0x100; off += 4)
                {
                  if (auto v = memory::read_f32(fd, hk + off))
                  {
                    if (std::abs(*v - 1.0f) < 0.1f && *v > 0.0f && *v < 10.0f)
                    {
                      if (!G_DUMPER.get_offset("HumanoidDescription", "HeadScale"))
                      {
                        G_DUMPER.add_offset("HumanoidDescription", "HeadScale", off);
                      }
                      else if (!G_DUMPER.get_offset("HumanoidDescription", "TorsoScale"))
                      {
                        G_DUMPER.add_offset("HumanoidDescription", "TorsoScale", off);
                      }
                      else if (!G_DUMPER.get_offset("HumanoidDescription", "WaistScale"))
                      {
                        G_DUMPER.add_offset("HumanoidDescription", "WaistScale", off);
                      }
                      else
                      {
                        break;
                      }
                    }
                  }
                }
              }
            }
          }
          break;
        }
      }
    }
  }
} // namespace stages
