#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace stages
{
  static std::vector<size_t> collect_children(int fd, size_t addr, size_t cs, size_t ce)
  {
    std::vector<size_t> out;
    auto head = memory::read<size_t>(fd, addr + cs);
    if (!head)
      return out;
    auto first = memory::read<size_t>(fd, *head);
    if (!first)
      return out;
    auto last = memory::read<size_t>(fd, *head + ce);
    if (!last)
      return out;

    if (*first < 0x10000 || *last < 0x10000)
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

  static std::vector<size_t> find_primitive_addrs(int fd, size_t addr)
  {
    std::vector<size_t> out;
    for (size_t off = 0; off < 0x2000; off += 8)
    {
      auto ptr = memory::read<size_t>(fd, addr + off);
      if (!ptr || *ptr < 0x10000)
        continue;
      if (auto r = rtti::scan_rtti(fd, *ptr))
      {
        if (r->name == "Primitive@RBX")
        {
          out.push_back(*ptr);
          if (out.size() >= 5)
            break;
        }
      }
    }
    return out;
  }

  static std::optional<size_t> find_primitive_offset(int fd, size_t addr, size_t cs, size_t ce)
  {
    std::vector<size_t> offsets;
    if (cs > 0 && ce > 0)
    {
      auto ws_children = collect_children(fd, addr, cs, ce);
      for (size_t child : ws_children)
      {
        if (auto po = rtti::find(fd, child, "Primitive@RBX", 0x1000, 8))
        {
          offsets.push_back(*po);
        }
      }
    }

    if (offsets.empty())
    {
      for (size_t off = 0; off < 0x200; off += 8)
      {
        auto ptr = memory::read<size_t>(fd, addr + off);
        if (!ptr || *ptr < 0x10000)
          continue;
        if (auto po = rtti::find(fd, *ptr, "Primitive@RBX", 0x1000, 8))
        {
          offsets.push_back(*po);
          if (offsets.size() >= 3)
            break;
        }
      }
    }

    if (offsets.empty())
      return std::nullopt;
    std::sort(offsets.begin(), offsets.end());
    return offsets.front();
  }

  static std::vector<std::pair<size_t, size_t>> collect_parts_and_prims(int fd, size_t addr, size_t cs, size_t ce, size_t po)
  {
    std::vector<std::pair<size_t, size_t>> out;
    if (cs > 0 && ce > 0)
    {
      auto ws_children = collect_children(fd, addr, cs, ce);
      for (size_t child : ws_children)
      {
        if (auto pa = memory::read<size_t>(fd, child + po))
        {
          if (*pa >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *pa))
            {
              if (r->name == "Primitive@RBX")
              {
                out.push_back({ child, po });
                if (out.size() >= 3)
                  break;
              }
            }
          }
        }
      }
    }
    if (out.empty())
    {
      auto prims = find_primitive_addrs(fd, addr);
      for (size_t p : prims)
      {
        for (size_t off = 0; off < 0x200; off += 8)
        {
          auto ptr = memory::read<size_t>(fd, addr + off);
          if (!ptr || *ptr < 0x10000)
            continue;
          if (auto pa = memory::read<size_t>(fd, *ptr + po))
          {
            if (*pa == p)
            {
              out.push_back({ *ptr, po });
              break;
            }
          }
        }
      }
    }
    return out;
  }

  static void dump_base_part_props(int fd, size_t bp_addr)
  {
    for (size_t off = 0; off < 0x100; off += 4)
    {
      auto v = memory::read_f32(fd, bp_addr + off);
      if (!v)
        continue;
      if (std::abs(*v) < 0.01f)
      {
        G_DUMPER.add_offset("BasePart", "Reflectance", off);
        std::cerr << "  BasePart::Reflectance at +0x" << std::hex << off << std::dec << "\n";
        break;
      }
    }

    for (size_t off = 0; off < 0x100; off += 4)
    {
      auto v = memory::read_f32(fd, bp_addr + off);
      if (!v)
        continue;
      if (std::abs(*v) < 0.01f)
      {
        size_t r_off = G_DUMPER.get_offset("BasePart", "Reflectance").value_or(-1);
        if (off != r_off)
        {
          G_DUMPER.add_offset("BasePart", "Transparency", off);
          std::cerr << "  BasePart::Transparency at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 1)
    {
      auto v = memory::read<uint8_t>(fd, bp_addr + off);
      if (!v)
        continue;
      if (*v <= 4)
      {
        auto v4 = memory::read<uint32_t>(fd, bp_addr + off - (off % 4));
        if (!v4)
          continue;
        if ((*v4 & 0xFF) == *v && *v4 < 0x1000)
        {
          G_DUMPER.add_offset("BasePart", "Shape", off);
          std::cerr << "  BasePart::Shape at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x100; off += 1)
    {
      auto v = memory::read<uint8_t>(fd, bp_addr + off);
      if (v && *v == 1)
      {
        G_DUMPER.add_offset("BasePart", "CastShadow", off);
        std::cerr << "  BasePart::CastShadow at +0x" << std::hex << off << std::dec << "\n";
        break;
      }
    }

    for (size_t off = 0; off < 0x100; off += 1)
    {
      auto v = memory::read<uint8_t>(fd, bp_addr + off);
      if (v && *v == 0)
      {
        size_t cast = G_DUMPER.get_offset("BasePart", "CastShadow").value_or(-1);
        if (off != cast && std::abs(static_cast<long long>(off) - static_cast<long long>(cast)) <= 4)
        {
          G_DUMPER.add_offset("BasePart", "Locked", off);
          std::cerr << "  BasePart::Locked at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x100; off += 1)
    {
      auto v = memory::read<uint8_t>(fd, bp_addr + off);
      if (v && *v == 0)
      {
        size_t locked = G_DUMPER.get_offset("BasePart", "Locked").value_or(-1);
        size_t cast = G_DUMPER.get_offset("BasePart", "CastShadow").value_or(-1);
        if (off != locked && off != cast &&
            (std::abs(static_cast<long long>(off) - static_cast<long long>(locked)) <= 8 ||
             std::abs(static_cast<long long>(off) - static_cast<long long>(cast)) <= 8))
        {
          G_DUMPER.add_offset("BasePart", "Massless", off);
          std::cerr << "  BasePart::Massless at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }
  }

  static void dump_primitive_props(int fd, const std::vector<size_t> &prim_addrs)
  {
    if (prim_addrs.empty())
      return;
    size_t base = prim_addrs[0];

    for (size_t off = 0x80; off < 0x180; off += 4)
    {
      auto v = memory::read<std::array<float, 3>>(fd, base + off);
      if (!v)
        continue;
      if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
        continue;
      if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
        continue;
      if (std::abs((*v)[0]) > 1000.0f || std::abs((*v)[1]) > 1000.0f || std::abs((*v)[2]) > 1000.0f)
        continue;

      if (prim_addrs.size() >= 2)
      {
        bool varies = false;
        for (size_t i = 1; i < prim_addrs.size(); ++i)
        {
          if (auto ov = memory::read<std::array<float, 3>>(fd, prim_addrs[i] + off))
          {
            if (std::abs((*ov)[0] - (*v)[0]) > 0.01f || std::abs((*ov)[1] - (*v)[1]) > 0.01f || std::abs((*ov)[2] - (*v)[2]) > 0.01f)
            {
              varies = true;
              break;
            }
          }
        }
        if (varies)
        {
          G_DUMPER.add_offset("Primitive", "AssemblyLinearVelocity", off);
          std::cerr << "  Primitive::AssemblyLinearVelocity at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0x80; off < 0x180; off += 4)
    {
      auto v = memory::read<std::array<float, 3>>(fd, base + off);
      if (!v)
        continue;
      if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
        continue;
      if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
        continue;
      if (std::abs((*v)[0]) > 100.0f || std::abs((*v)[1]) > 100.0f || std::abs((*v)[2]) > 100.0f)
        continue;

      size_t lv_off = G_DUMPER.get_offset("Primitive", "AssemblyLinearVelocity").value_or(-1);
      if (off == lv_off)
        continue;

      if (prim_addrs.size() >= 2)
      {
        bool varies = false;
        for (size_t i = 1; i < prim_addrs.size(); ++i)
        {
          if (auto ov = memory::read<std::array<float, 3>>(fd, prim_addrs[i] + off))
          {
            if (std::abs((*ov)[0] - (*v)[0]) > 0.01f || std::abs((*ov)[1] - (*v)[1]) > 0.01f || std::abs((*ov)[2] - (*v)[2]) > 0.01f)
            {
              varies = true;
              break;
            }
          }
        }
        if (varies)
        {
          G_DUMPER.add_offset("Primitive", "AssemblyAngularVelocity", off);
          std::cerr << "  Primitive::AssemblyAngularVelocity at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 1)
    {
      auto v = memory::read<uint8_t>(fd, base + off);
      if (v && *v >= 1 && *v <= 40)
      {
        auto v4 = memory::read<uint32_t>(fd, base + off - (off % 4));
        if (v4 && (*v4 & 0xFF) == *v && *v4 < 0x10000)
        {
          G_DUMPER.add_offset("Primitive", "Material", off);
          std::cerr << "  Primitive::Material at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 1)
    {
      auto v = memory::read<uint8_t>(fd, base + off);
      if (v && (*v == 0x81 || *v == 0x01 || *v == 0x83 || *v == 0x85 || *v == 0x87))
      {
        auto v4 = memory::read<uint32_t>(fd, base + off - (off % 4));
        if (v4 && (*v4 & 0xFF) == *v && *v4 < 0x1000)
        {
          G_DUMPER.add_offset("Primitive", "PrimitiveFlags", off);
          std::cerr << "  Primitive::PrimitiveFlags at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }
  }

  void base_part(int fd)
  {
    std::cerr << "[base_part/primitive]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    auto po_opt = find_primitive_offset(fd, ws_addr, cs, ce);
    if (!po_opt)
    {
      std::cerr << "  No BasePart/Primitive found\n";
      return;
    }
    size_t po = *po_opt;
    G_DUMPER.add_offset("BasePart", "Primitive", po);
    std::cerr << "  BasePart::Primitive at +0x" << std::hex << po << std::dec << "\n";

    auto parts = collect_parts_and_prims(fd, ws_addr, cs, ce, po);
    std::vector<size_t> prim_addrs;
    for (const auto &[bp_addr, _] : parts)
    {
      if (auto pa = memory::read<size_t>(fd, bp_addr + po))
      {
        if (*pa >= 0x10000)
          prim_addrs.push_back(*pa);
      }
    }
    if (prim_addrs.empty())
    {
      auto direct = find_primitive_addrs(fd, ws_addr);
      prim_addrs.insert(prim_addrs.end(), direct.begin(), direct.end());
    }

    if (!prim_addrs.empty())
    {
      size_t base = prim_addrs.front();
      std::vector<size_t> pos_candidates;

      for (size_t off = 0x80; off < 0x200; off += 4)
      {
        auto v = memory::read<std::array<float, 3>>(fd, base + off);
        if (!v)
          continue;
        if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
          continue;
        if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
          continue;
        if (std::abs((*v)[0]) < 1e8f && std::abs((*v)[1]) < 1e8f && std::abs((*v)[2]) < 1e8f &&
            (std::abs((*v)[0]) > 1.0f || std::abs((*v)[1]) > 1.0f || std::abs((*v)[2]) > 1.0f))
        {
          if (off >= 36)
          {
            size_t rot_start = off - 36;
            if (auto r = memory::read<std::array<float, 9>>(fd, base + rot_start))
            {
              bool ok = true;
              for (float x : *r)
                if (std::isnan(x) || std::isinf(x))
                {
                  ok = false;
                  break;
                }
              if (ok)
              {
                bool ortho = true;
                for (int i = 0; i < 3; ++i)
                {
                  float a0 = (*r)[i * 3];
                  float a1 = (*r)[i * 3 + 1];
                  float a2 = (*r)[i * 3 + 2];
                  float len = std::sqrt(a0 * a0 + a1 * a1 + a2 * a2);
                  if (std::abs(len - 1.0f) > 0.02f)
                  {
                    ortho = false;
                    break;
                  }
                  if (std::abs(a0) > 1.5f || std::abs(a1) > 1.5f || std::abs(a2) > 1.5f)
                  {
                    ortho = false;
                    break;
                  }
                }
                if (ortho)
                {
                  float dot01 = (*r)[0] * (*r)[3] + (*r)[1] * (*r)[4] + (*r)[2] * (*r)[5];
                  float dot02 = (*r)[0] * (*r)[6] + (*r)[1] * (*r)[7] + (*r)[2] * (*r)[8];
                  if (std::abs(dot01) < 0.02f && std::abs(dot02) < 0.02f)
                  {
                    float det = (*r)[0] * ((*r)[4] * (*r)[8] - (*r)[5] * (*r)[7]) - (*r)[3] * ((*r)[1] * (*r)[8] - (*r)[2] * (*r)[7]) +
                                (*r)[6] * ((*r)[1] * (*r)[5] - (*r)[2] * (*r)[4]);
                    if (std::abs(det - 1.0f) < 0.02f)
                    {
                      pos_candidates.push_back(off);
                    }
                  }
                }
              }
            }
          }
        }
      }

      if (!pos_candidates.empty())
      {
        size_t best_pos = pos_candidates.front();
        G_DUMPER.add_offset("Primitive", "Position", best_pos);
        G_DUMPER.add_offset("Primitive", "CFrame", best_pos - 36);
        G_DUMPER.add_offset("Primitive", "Rotation", best_pos - 36);
        G_DUMPER.add_offset("Primitive", "Orientation", best_pos - 36);
        std::cerr << "  Primitive::Position at +0x" << std::hex << best_pos << ", CFrame at +0x" << (best_pos - 36) << std::dec << "\n";

        size_t co = best_pos - 36;
        size_t start = co + 0x80;
        size_t end = co + 0x110;
        std::vector<std::pair<size_t, float>> size_candidates;

        size_t off = start;
        while (off < end)
        {
          auto v = memory::read<std::array<float, 3>>(fd, base + off);
          if (!v)
          {
            off += 4;
            continue;
          }

          bool bad = false;
          for (float x : *v)
          {
            if (std::isnan(x) || std::isinf(x) || x <= 0.0f || x > 1000.0f)
              bad = true;
          }
          if (bad)
          {
            off += 4;
            continue;
          }

          if (std::abs((*v)[0] - 1.0f) < 0.01f && std::abs((*v)[1] - 1.0f) < 0.01f && std::abs((*v)[2] - 1.0f) < 0.01f)
          {
            off += 4;
            continue;
          }

          int diff_count = 0;
          int total_pairs = 0;
          for (size_t i = 0; i < prim_addrs.size(); ++i)
          {
            for (size_t j = i + 1; j < prim_addrs.size(); ++j)
            {
              total_pairs++;
              auto vi = memory::read<std::array<float, 3>>(fd, prim_addrs[i] + off);
              auto vj = memory::read<std::array<float, 3>>(fd, prim_addrs[j] + off);
              if (vi && vj)
              {
                if (std::abs((*vi)[0] - (*vj)[0]) > 0.01f || std::abs((*vi)[1] - (*vj)[1]) > 0.01f || std::abs((*vi)[2] - (*vj)[2]) > 0.01f)
                {
                  diff_count++;
                }
              }
            }
          }
          float score = total_pairs > 0 ? (float)diff_count / (float)total_pairs : 1.0f;
          if (score > 0.5f || total_pairs == 0)
          {
            size_candidates.push_back({ off, score });
          }
          off += 4;
        }

        if (!size_candidates.empty())
        {
          std::sort(size_candidates.begin(), size_candidates.end(),
                    [](const auto &a, const auto &b)
                    {
                      if (std::abs(a.second - b.second) > 0.001f)
                        return a.second > b.second;
                      return a.first < b.first;
                    });
          size_t sz = size_candidates[0].first;
          G_DUMPER.add_offset("Primitive", "Size", sz);
          std::cerr << "  Primitive::Size at +0x" << std::hex << sz << std::dec << "\n";
        }
      }

      dump_primitive_props(fd, prim_addrs);
    }

    if (!parts.empty())
    {
      dump_base_part_props(fd, parts.front().first);
    }
  }
} // namespace stages
