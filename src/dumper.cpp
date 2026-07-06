#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"

Dumper G_DUMPER;
size_t G_VISUAL_ENGINE = 0;
size_t G_DATA_MODEL_ADDR = 0;
size_t G_WORKSPACE_ADDR = 0;

void Dumper::add_offset(const std::string &ns, const std::string &name, size_t offset)
{
  std::lock_guard<std::mutex> lock(mtx);
  auto &entries = offsets[ns];
  for (auto &e : entries)
  {
    if (e.name == name)
    {
      if (!is_baseline_mode)
      {
        if (e.offset == offset)
        {
          e.status = OffsetStatus::UNCHANGED;
        }
        else
        {
          e.status = OffsetStatus::CHANGED;
          e.offset = offset;
        }
      }
      else
      {
        e.offset = offset;
      }
      return;
    }
  }
  OffsetStatus st = is_baseline_mode ? OffsetStatus::BASELINE : OffsetStatus::NEW;
  entries.push_back({ name, offset, st });
}

void Dumper::add_offset_val(const std::string &ns, const std::string &name, size_t offset, uint64_t value)
{
  add_offset(ns, name, offset);
  std::string key = ns + "::" + name;
  std::lock_guard<std::mutex> lock(mtx);
  values[key] = value;
}

std::optional<size_t> Dumper::get_offset(const std::string &ns, const std::string &name)
{
  std::lock_guard<std::mutex> lock(mtx);
  auto it = offsets.find(ns);
  if (it != offsets.end())
  {
    for (const auto &e : it->second)
    {
      if (e.name == name)
      {
        return e.offset;
      }
    }
  }
  return std::nullopt;
}

std::optional<uint64_t> Dumper::get_value(const std::string &ns, const std::string &name)
{
  std::string key = ns + "::" + name;
  std::lock_guard<std::mutex> lock(mtx);
  auto it = values.find(key);
  if (it != values.end())
  {
    return it->second;
  }
  return std::nullopt;
}

const std::map<std::string, std::vector<OffsetEntry>> &Dumper::get_all_offsets()
{
  return offsets;
}

std::vector<size_t> collect_children(int fd, size_t addr)
{
  std::vector<size_t> out;
  auto cs_opt = G_DUMPER.get_offset("Instance", "ChildrenStart");
  auto ce_opt = G_DUMPER.get_offset("Instance", "ChildrenEnd");

  if (!cs_opt || !ce_opt)
    return out;
  size_t cs = *cs_opt;
  size_t ce = *ce_opt;

  if (cs == 0 || ce == 0)
    return out;

  auto llist_opt = memory::read<size_t>(fd, addr + cs);
  if (!llist_opt || *llist_opt < 0x10000)
    return out;
  size_t llist = *llist_opt;

  auto sentinel_opt = memory::read<size_t>(fd, llist + ce);
  if (!sentinel_opt || *sentinel_opt < 0x10000)
    return out;
  size_t sentinel = *sentinel_opt;

  auto first_node_opt = memory::read<size_t>(fd, llist);
  if (!first_node_opt || *first_node_opt < 0x10000)
    return out;
  size_t node = *first_node_opt;

  for (int i = 0; i < 500; ++i)
  {
    if (node == sentinel || node == 0)
      break;
    if (auto child_opt = memory::read<size_t>(fd, node))
    {
      if (*child_opt >= 0x10000)
      {
        out.push_back(*child_opt);
      }
    }
    node += 0x10;
  }
  return out;
}

std::vector<size_t> find_instances(int fd, size_t root, const std::string &class_name, size_t max_count)
{
  std::vector<size_t> out;
  std::vector<size_t> stack;
  stack.push_back(root);

  while (!stack.empty())
  {
    if (out.size() >= max_count)
      break;
    size_t addr = stack.back();
    stack.pop_back();

    auto kids = collect_children(fd, addr);
    for (size_t k : kids)
    {
      if (auto r = rtti::scan_rtti(fd, k))
      {
        if (r->name == class_name)
        {
          out.push_back(k);
          if (out.size() >= max_count)
            break;
        }
      }
      stack.push_back(k);
    }
  }
  return out;
}
