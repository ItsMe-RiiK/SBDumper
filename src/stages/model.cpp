#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"

#include <algorithm>
#include <iostream>
#include <vector>

namespace stages
{
  static std::vector<size_t> collect_children(int fd, size_t addr, size_t cs, size_t ce)
  {
    std::vector<size_t> out;
    auto head = memory::read<size_t>(fd, addr + cs);
    if (!head)
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

  static std::vector<size_t> find_models(int fd, size_t ws_addr, size_t cs, size_t ce)
  {
    std::vector<size_t> out;
    if (cs > 0 && ce > 0)
    {
      auto ws_children = collect_children(fd, ws_addr, cs, ce);
      for (size_t child : ws_children)
      {
        if (auto r = rtti::scan_rtti(fd, child))
        {
          if (r->name == "Model@RBX" || r->name == "ModelInstance@RBX")
          {
            out.push_back(child);
          }
        }
      }
    }
    if (out.empty())
    {
      for (size_t off = 0; off < 0x2000; off += 8)
      {
        auto ptr = memory::read<size_t>(fd, ws_addr + off);
        if (!ptr || *ptr < 0x10000)
          continue;
        if (auto r = rtti::scan_rtti(fd, *ptr))
        {
          if (r->name == "Model@RBX" || r->name == "ModelInstance@RBX")
          {
            out.push_back(*ptr);
            if (out.size() >= 3)
              break;
          }
        }
      }
    }
    return out;
  }

  void model(int fd)
  {
    std::cerr << "[model]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);
    auto models = find_models(fd, ws_addr, cs, ce);

    std::vector<std::vector<size_t>> model_children;
    for (size_t m : models)
    {
      if (cs > 0 && ce > 0)
      {
        model_children.push_back(collect_children(fd, m, cs, ce));
      }
      else
      {
        model_children.push_back({});
      }
    }

    for (size_t idx = 0; idx < models.size(); ++idx)
    {
      if (G_DUMPER.get_offset("Model", "PrimaryPart"))
        break;
      size_t model_addr = models[idx];

      for (size_t off = 0; off < 0x300; off += 8)
      {
        auto ptr = memory::read<size_t>(fd, model_addr + off);
        if (!ptr || *ptr < 0x10000)
          continue;

        if (rtti::find(fd, *ptr, "Primitive@RBX", 0x1000, 8))
        {
          bool is_child = false;
          const auto &kids = model_children[idx];
          if (std::find(kids.begin(), kids.end(), *ptr) != kids.end())
          {
            is_child = true;
          }

          if (is_child || kids.empty())
          {
            G_DUMPER.add_offset("Model", "PrimaryPart", off);
            std::cerr << "  Model::PrimaryPart at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }
  }
} // namespace stages
