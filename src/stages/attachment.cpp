#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"

#include <cmath>
#include <iostream>
#include <vector>

namespace stages
{
  static std::vector<size_t> find_instances(int fd, size_t addr, const char *name, size_t max_count)
  {
    std::vector<size_t> out;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    if (cs > 0 && ce > 0)
    {
      auto head = memory::read<size_t>(fd, addr + cs);
      if (head && *head >= 0x10000)
      {
        auto first = memory::read<size_t>(fd, *head);
        auto last = memory::read<size_t>(fd, *head + ce);
        if (first && last && *first >= 0x10000 && *last >= 0x10000)
        {
          size_t node = *first;
          for (int i = 0; i < 500; ++i)
          {
            if (node == *last || node == 0)
              break;
            if (auto child = memory::read<size_t>(fd, node))
            {
              if (*child >= 0x10000)
              {
                if (auto r = rtti::scan_rtti(fd, *child))
                {
                  if (r->name == name)
                  {
                    out.push_back(*child);
                    if (out.size() >= max_count)
                      return out;
                  }
                }
              }
            }
            node += 0x10;
          }
        }
      }
    }
    return out;
  }

  void attachment(int fd)
  {
    std::cerr << "[attachment]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;

    auto atts = find_instances(fd, ws_addr, "Attachment@RBX", 3);
    if (atts.empty())
    {
      for (size_t off = 0; off < 0x4000; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, ws_addr + off))
        {
          if (*ptr >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *ptr))
            {
              if (r->name == "Attachment@RBX")
              {
                atts.push_back(*ptr);
                if (atts.size() >= 3)
                  break;
              }
            }
          }
        }
      }
    }
    if (atts.empty())
    {
      std::cerr << "  No Attachment found\n";
      return;
    }
    std::cerr << "  Found " << atts.size() << " Attachment(s)\n";

    size_t a = atts[0];

    for (size_t off = 0; off < 0x80; off += 4)
    {
      if (auto v = memory::read<std::array<float, 3>>(fd, a + off))
      {
        if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
          continue;
        if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
          continue;
        if (std::abs((*v)[0]) < 1000000.0f && std::abs((*v)[1]) < 1000000.0f && std::abs((*v)[2]) < 1000000.0f)
        {
          if (off < 36)
            continue;
          size_t rot_off = off - 36;
          if (rot_off < off && rot_off > 0)
          {
            if (auto r = memory::read<std::array<float, 9>>(fd, a + rot_off))
            {
              bool ok = true;
              for (float x : *r)
                if (std::isnan(x) || std::isinf(x))
                  ok = false;
              if (ok)
              {
                bool ortho = true;
                for (int i = 0; i < 3; ++i)
                {
                  float len = std::sqrt((*r)[i * 3] * (*r)[i * 3] + (*r)[i * 3 + 1] * (*r)[i * 3 + 1] + (*r)[i * 3 + 2] * (*r)[i * 3 + 2]);
                  if (std::abs(len - 1.0f) > 0.02f)
                  {
                    ortho = false;
                    break;
                  }
                }
                if (ortho)
                {
                  G_DUMPER.add_offset("Attachment", "CFrame", rot_off);
                  G_DUMPER.add_offset("Attachment", "Position", off);
                  std::cerr << "  Attachment::CFrame at +0x" << std::hex << rot_off << ", Position at +0x" << off << std::dec << "\n";
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
      if (auto v = memory::read<std::array<float, 3>>(fd, a + off))
      {
        if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
          continue;
        if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
          continue;
        float len = std::sqrt((*v)[0] * (*v)[0] + (*v)[1] * (*v)[1] + (*v)[2] * (*v)[2]);
        if (std::abs(len - 1.0f) < 0.02f && std::abs((*v)[0]) <= 1.0f && std::abs((*v)[1]) <= 1.0f && std::abs((*v)[2]) <= 1.0f)
        {
          size_t pos_off = G_DUMPER.get_offset("Attachment", "Position").value_or(-1);
          size_t cframe = G_DUMPER.get_offset("Attachment", "CFrame").value_or(-1);
          bool near_cframe = (cframe != (size_t)-1) && (off == cframe || off == cframe + 4 || off == cframe + 8);
          if (off != pos_off && !near_cframe)
          {
            G_DUMPER.add_offset("Attachment", "Axis", off);
            std::cerr << "  Attachment::Axis at +0x" << std::hex << off << std::dec << "\n";
            for (size_t off2 = off + 12; off2 < off + 24; off2 += 4)
            {
              if (auto v2 = memory::read<std::array<float, 3>>(fd, a + off2))
              {
                float len2 = std::sqrt((*v2)[0] * (*v2)[0] + (*v2)[1] * (*v2)[1] + (*v2)[2] * (*v2)[2]);
                if (std::abs(len2 - 1.0f) < 0.02f)
                {
                  G_DUMPER.add_offset("Attachment", "SecondaryAxis", off2);
                  std::cerr << "  Attachment::SecondaryAxis at +0x" << std::hex << off2 << std::dec << "\n";
                  break;
                }
              }
            }
            break;
          }
        }
      }
    }

    for (size_t off = 0; off < 0x100; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, a + off))
      {
        if (*v == 1)
        {
          if (auto pos = G_DUMPER.get_offset("Attachment", "Position"))
          {
            if (off >= *pos && off <= *pos + 3)
              continue;
          }
          G_DUMPER.add_offset("Attachment", "Visible", off);
          std::cerr << "  Attachment::Visible at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    if (atts.size() >= 2)
    {
      size_t a2 = atts[1];
      if (auto pos = G_DUMPER.get_offset("Attachment", "Position"))
      {
        if (auto p1 = memory::read<std::array<float, 3>>(fd, a + *pos))
        {
          if (auto p2 = memory::read<std::array<float, 3>>(fd, a2 + *pos))
          {
            float diff = std::abs((*p1)[0] - (*p2)[0]) + std::abs((*p1)[1] - (*p2)[1]) + std::abs((*p1)[2] - (*p2)[2]);
            if (diff > 0.01f)
            {
              for (size_t wp_off = 0; wp_off < 0x100; wp_off += 4)
              {
                if (wp_off == *pos || wp_off == *pos + 4 || wp_off == *pos + 8)
                  continue;
                if (auto wpa = memory::read<std::array<float, 3>>(fd, a + wp_off))
                {
                  if (std::abs((*wpa)[0] - (*p1)[0]) < 0.01f && std::abs((*wpa)[1] - (*p1)[1]) < 0.01f &&
                      std::abs((*wpa)[2] - (*p1)[2]) < 0.01f)
                    continue;
                  if (auto wpa2 = memory::read<std::array<float, 3>>(fd, a2 + wp_off))
                  {
                    if (std::abs((*wpa2)[0] - (*p2)[0]) > 0.01f || std::abs((*wpa2)[1] - (*p2)[1]) > 0.01f ||
                        std::abs((*wpa2)[2] - (*p2)[2]) > 0.01f)
                    {
                      G_DUMPER.add_offset("Attachment", "WorldPosition", wp_off);
                      std::cerr << "  Attachment::WorldPosition at +0x" << std::hex << wp_off << std::dec << "\n";
                      break;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
} // namespace stages
