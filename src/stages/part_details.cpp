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

  static std::vector<size_t> find_base_parts(int fd, size_t ws_addr, size_t cs, size_t ce)
  {
    std::vector<size_t> out;
    if (cs > 0 && ce > 0)
    {
      auto ws_kids = collect_children(fd, ws_addr, cs, ce);
      for (size_t child : ws_kids)
      {
        if (auto r = rtti::scan_rtti(fd, child))
        {
          if (r->name == "Part@RBX" || r->name == "WedgePart@RBX" || r->name == "CylinderPart@RBX" || r->name == "CornerWedgePart@RBX" ||
              r->name == "TrussPart@RBX" || r->name == "Seat@RBX" || r->name == "VehicleSeat@RBX" || r->name == "SpawnLocation@RBX" ||
              r->name == "FlagStand@RBX" || r->name == "SkateboardPlatform@RBX")
          {
            out.push_back(child);
            if (out.size() >= 5)
              break;
          }
        }
        auto grandkids = collect_children(fd, child, cs, ce);
        for (size_t gk : grandkids)
        {
          if (auto r = rtti::scan_rtti(fd, gk))
          {
            if (r->name == "Part@RBX" || r->name == "WedgePart@RBX" || r->name == "CylinderPart@RBX" || r->name == "CornerWedgePart@RBX" ||
                r->name == "TrussPart@RBX")
            {
              out.push_back(gk);
              if (out.size() >= 5)
                break;
            }
          }
        }
        if (out.size() >= 5)
          break;
      }
    }
    if (out.empty())
    {
      for (size_t off = 0; off < 0x4000; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, ws_addr + off))
        {
          if (*ptr >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *ptr))
            {
              std::string name = r->name.substr(0, r->name.find('@'));
              if (name == "Part" || name == "WedgePart" || name == "CylinderPart")
              {
                out.push_back(*ptr);
                if (out.size() >= 5)
                  break;
              }
            }
          }
        }
      }
    }
    return out;
  }

  static void dump_color3(int fd, const std::vector<size_t> &parts)
  {
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto first_c = memory::read<std::array<float, 3>>(fd, parts[0] + off))
      {
        if (std::isnan((*first_c)[0]) || std::isnan((*first_c)[1]) || std::isnan((*first_c)[2]))
          continue;
        if (std::isinf((*first_c)[0]) || std::isinf((*first_c)[1]) || std::isinf((*first_c)[2]))
          continue;
        if ((*first_c)[0] < 0.0f || (*first_c)[0] > 1.0f || (*first_c)[1] < 0.0f || (*first_c)[1] > 1.0f || (*first_c)[2] < 0.0f ||
            (*first_c)[2] > 1.0f)
          continue;
        if ((*first_c)[0] + (*first_c)[1] + (*first_c)[2] < 0.01f)
          continue;

        if (parts.size() >= 2)
        {
          if (auto sc = memory::read<std::array<float, 3>>(fd, parts[1] + off))
          {
            if ((*sc)[0] >= 0.0f && (*sc)[0] <= 1.0f && (*sc)[1] >= 0.0f && (*sc)[1] <= 1.0f && (*sc)[2] >= 0.0f && (*sc)[2] <= 1.0f)
            {
              if (std::abs((*sc)[0] - (*first_c)[0]) > 0.01f || std::abs((*sc)[1] - (*first_c)[1]) > 0.01f ||
                  std::abs((*sc)[2] - (*first_c)[2]) > 0.01f)
              {
                G_DUMPER.add_offset("BasePart", "Color", off);
                std::cerr << "  BasePart::Color3 at +0x" << std::hex << off << std::dec << "\n";
                return;
              }
            }
          }
        }
        else
        {
          G_DUMPER.add_offset("BasePart", "Color", off);
          std::cerr << "  BasePart::Color3 at +0x" << std::hex << off << std::dec << "\n";
          return;
        }
      }
    }
  }

  static void dump_size_on_bp(int fd, const std::vector<size_t> &parts)
  {
    std::vector<std::pair<size_t, uint32_t>> candidates;
    for (size_t off = 0; off < 0x200; off += 4)
    {
      uint32_t valid = 0;
      for (size_t p : parts)
      {
        if (auto v = memory::read<std::array<float, 3>>(fd, p + off))
        {
          if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
            continue;
          if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
            continue;
          if ((*v)[0] <= 0.0f || (*v)[0] > 10000.0f || (*v)[1] <= 0.0f || (*v)[1] > 10000.0f || (*v)[2] <= 0.0f || (*v)[2] > 10000.0f)
            continue;
          valid++;
        }
      }
      if (valid >= 2)
        candidates.push_back({ off, valid });
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    if (!candidates.empty())
    {
      G_DUMPER.add_offset("BasePart", "Size", candidates[0].first);
      std::cerr << "  BasePart::Size at +0x" << std::hex << candidates[0].first << std::dec << "\n";
    }
  }

  static void dump_material_on_bp(int fd, const std::vector<size_t> &parts)
  {
    for (size_t off = 0; off < 0x200; off += 1)
    {
      uint32_t valid = 0;
      for (size_t p : parts)
      {
        if (auto v = memory::read<uint8_t>(fd, p + off))
        {
          if (*v >= 1 && *v <= 45)
          {
            if (auto v4 = memory::read<uint32_t>(fd, p + off - (off % 4)))
            {
              if ((*v4 & 0xFF) == *v && *v4 < 0x10000)
                valid++;
            }
          }
        }
      }
      if (valid >= 2)
      {
        G_DUMPER.add_offset("BasePart", "Material", off);
        std::cerr << "  BasePart::Material at +0x" << std::hex << off << std::dec << "\n";
        return;
      }
    }
  }

  static void dump_bool_flags(int fd, const std::vector<size_t> &parts)
  {
    const char *bool_names[] = { "Anchored", "CanCollide", "CanQuery", "CanTouch" };
    size_t ref_off = G_DUMPER.get_offset("BasePart", "CastShadow").value_or(0xED);
    size_t locked_off = G_DUMPER.get_offset("BasePart", "Locked").value_or(0xEE);
    uint32_t names_found = 0;

    for (size_t off = 0; off < 0x200; off += 1)
    {
      if (names_found == 4)
        break;
      if (off == ref_off || off == locked_off)
        continue;

      std::vector<uint8_t> vals;
      for (size_t p : parts)
      {
        if (auto v = memory::read<uint8_t>(fd, p + off))
          vals.push_back(*v);
      }
      if (vals.size() < 2)
        continue;

      bool is_bool_field = true;
      for (uint8_t v : vals)
        if (v != 0 && v != 1)
          is_bool_field = false;
      if (!is_bool_field)
        continue;

      G_DUMPER.add_offset("BasePart", bool_names[names_found % 4], off);
      std::cerr << "  BasePart::" << bool_names[names_found % 4] << " at +0x" << std::hex << off << std::dec << "\n";
      names_found++;

      for (size_t adj_off = off + 1; adj_off < off + 8; adj_off += 1)
      {
        if (names_found == 4)
          break;
        std::vector<uint8_t> adj_vals;
        for (size_t p : parts)
        {
          if (auto v = memory::read<uint8_t>(fd, p + adj_off))
            adj_vals.push_back(*v);
        }
        if (adj_vals.size() >= 2)
        {
          bool adj_is_bool = true;
          for (uint8_t v : adj_vals)
            if (v != 0 && v != 1)
              adj_is_bool = false;
          if (adj_is_bool)
          {
            G_DUMPER.add_offset("BasePart", bool_names[names_found % 4], adj_off);
            std::cerr << "  BasePart::" << bool_names[names_found % 4] << " at +0x" << std::hex << adj_off << std::dec << "\n";
            names_found++;
          }
        }
      }
    }
  }

  static void dump_part_properties(int fd, size_t ws_addr, size_t cs, size_t ce)
  {
    auto parts = find_base_parts(fd, ws_addr, cs, ce);
    if (parts.empty())
    {
      std::cerr << "  No Part instances found for property scan\n";
      return;
    }
    std::cerr << "  Found " << parts.size() << " BasePart instance(s) for property scan\n";

    size_t po = G_DUMPER.get_offset("BasePart", "Primitive").value_or(0);
    if (po > 0)
    {
      std::vector<size_t> verified;
      for (size_t p : parts)
      {
        if (auto prim = memory::read<size_t>(fd, p + po))
        {
          if (*prim >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *prim))
            {
              if (r->name == "Primitive@RBX")
                verified.push_back(p);
            }
          }
        }
      }
      if (verified.size() >= 2)
      {
        dump_color3(fd, verified);
        dump_size_on_bp(fd, verified);
        dump_material_on_bp(fd, verified);
        dump_bool_flags(fd, verified);
      }
    }

    if (!G_DUMPER.get_offset("BasePart", "Color"))
      dump_color3(fd, parts);
    if (!G_DUMPER.get_offset("BasePart", "Size"))
      dump_size_on_bp(fd, parts);
    if (!G_DUMPER.get_offset("BasePart", "Material"))
      dump_material_on_bp(fd, parts);
    if (!G_DUMPER.get_offset("BasePart", "Anchored"))
      dump_bool_flags(fd, parts);
  }

  static void dump_primitive_details(int fd, size_t ws_addr, size_t cs, size_t ce)
  {
    size_t po = G_DUMPER.get_offset("BasePart", "Primitive").value_or(0);
    if (po == 0)
      return;

    std::vector<size_t> prim_addrs;
    if (cs > 0 && ce > 0)
    {
      auto ws_kids = collect_children(fd, ws_addr, cs, ce);
      for (size_t child : ws_kids)
      {
        if (auto prim_ptr = memory::read<size_t>(fd, child + po))
        {
          if (*prim_ptr >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *prim_ptr))
            {
              if (r->name == "Primitive@RBX")
              {
                prim_addrs.push_back(*prim_ptr);
                if (prim_addrs.size() >= 3)
                  break;
              }
            }
          }
        }
      }
    }
    if (prim_addrs.empty())
      return;

    size_t pf_off = G_DUMPER.get_offset("Primitive", "PrimitiveFlags").value_or(0);
    if (pf_off > 0)
    {
      G_DUMPER.add_offset("PrimitiveFlags", "Anchored", 0x80);
      G_DUMPER.add_offset("PrimitiveFlags", "CanCollide", 0x01);
      G_DUMPER.add_offset("PrimitiveFlags", "CanTouch", 0x02);
      G_DUMPER.add_offset("PrimitiveFlags", "CanQuery", 0x04);
    }

    for (size_t off = 0x80; off < 0x200; off += 4)
    {
      if (auto v = memory::read<std::array<float, 3>>(fd, prim_addrs[0] + off))
      {
        if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isnan((*v)[2]))
          continue;
        if (std::isinf((*v)[0]) || std::isinf((*v)[1]) || std::isinf((*v)[2]))
          continue;
        if (std::abs((*v)[0]) > 1000.0f || std::abs((*v)[1]) > 1000.0f || std::abs((*v)[2]) > 1000.0f)
          continue;

        size_t lv = G_DUMPER.get_offset("Primitive", "AssemblyLinearVelocity").value_or(-1);
        size_t av = G_DUMPER.get_offset("Primitive", "AssemblyAngularVelocity").value_or(-1);
        if (off == lv || off == av)
          continue;

        if (prim_addrs.size() >= 2)
        {
          if (auto ov = memory::read<std::array<float, 3>>(fd, prim_addrs[1] + off))
          {
            if (std::abs((*ov)[0] - (*v)[0]) > 0.01f || std::abs((*ov)[1] - (*v)[1]) > 0.01f || std::abs((*ov)[2] - (*v)[2]) > 0.01f)
            {
              G_DUMPER.add_offset("Primitive", "AssemblyRotVelocity", off);
              std::cerr << "  Primitive::AssemblyRotVelocity at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, prim_addrs[0] + off))
      {
        if (std::isnan(*v) || std::isinf(*v) || !std::isnormal(*v) && *v != 0.0f || *v < 0.0f || *v > 1000000.0f)
          continue;

        if (prim_addrs.size() >= 2)
        {
          if (auto ov = memory::read_f32(fd, prim_addrs[1] + off))
          {
            if (std::abs(*ov - *v) > 0.1f)
            {
              G_DUMPER.add_offset("Primitive", "Mass", off);
              std::cerr << "  Primitive::Mass at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 4)
    {
      bool all_same = true;
      if (auto first = memory::read_f32(fd, prim_addrs[0] + off))
      {
        if (std::isnan(*first) || std::isinf(*first) || !std::isnormal(*first) && *first != 0.0f || *first < 0.0f || *first > 1.0f)
          continue;
        if (std::abs(*first - 0.3f) > 0.29f)
          continue;
        for (size_t i = 1; i < prim_addrs.size(); ++i)
        {
          if (auto v = memory::read_f32(fd, prim_addrs[i] + off))
          {
            if (std::abs(*v - *first) > 0.01f)
            {
              all_same = false;
              break;
            }
          }
        }
        if (all_same)
        {
          G_DUMPER.add_offset("Primitive", "Friction", off);
          std::cerr << "  Primitive::Friction at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 4)
    {
      bool all_same = true;
      if (auto first = memory::read_f32(fd, prim_addrs[0] + off))
      {
        if (std::isnan(*first) || std::isinf(*first) || !std::isnormal(*first) && *first != 0.0f || *first < 0.0f || *first > 1.0f)
          continue;
        if (std::abs(*first - 0.5f) > 0.49f)
          continue;
        for (size_t i = 1; i < prim_addrs.size(); ++i)
        {
          if (auto v = memory::read_f32(fd, prim_addrs[i] + off))
          {
            if (std::abs(*v - *first) > 0.01f)
            {
              all_same = false;
              break;
            }
          }
        }
        if (all_same)
        {
          G_DUMPER.add_offset("Primitive", "Elasticity", off);
          std::cerr << "  Primitive::Elasticity at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }
  }

  void part_details(int fd)
  {
    std::cerr << "[part_details]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);

    dump_part_properties(fd, ws_addr, cs, ce);
    dump_primitive_details(fd, ws_addr, cs, ce);
  }
} // namespace stages
