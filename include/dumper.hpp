#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

enum class OffsetStatus
{
  BASELINE,
  CHANGED,
  UNCHANGED,
  NEW
};

struct OffsetEntry
{
  std::string name;
  size_t offset;
  OffsetStatus status;
};

class Dumper
{
public:
  Dumper() = default;

  void set_baseline_mode(bool val)
  {
    is_baseline_mode = val;
  }

  void add_offset(const std::string &ns, const std::string &name, size_t offset);
  void add_offset_val(const std::string &ns, const std::string &name, size_t offset, uint64_t value);

  std::optional<size_t> get_offset(const std::string &ns, const std::string &name);
  std::optional<uint64_t> get_value(const std::string &ns, const std::string &name);

  const std::map<std::string, std::vector<OffsetEntry>> &get_all_offsets();

private:
  std::mutex mtx;
  std::map<std::string, std::vector<OffsetEntry>> offsets;
  std::map<std::string, uint64_t> values;
  bool is_baseline_mode = false;
};

extern Dumper G_DUMPER;
extern size_t G_VISUAL_ENGINE;
extern size_t G_DATA_MODEL_ADDR;
extern size_t G_WORKSPACE_ADDR;

std::vector<size_t> collect_children(int fd, size_t addr);
std::vector<size_t> find_instances(int fd, size_t root, const std::string &class_name, size_t max_count);
