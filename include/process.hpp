#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct MemRegion
{
  size_t start;
  size_t end;
  std::string perms;
  std::string path;
};

class Process
{
public:
  Process();
  ~Process();

  bool attach(const std::string &name);
  int get_pid() const;
  size_t get_module_base() const;
  int get_mem_file() const;

  std::optional<std::pair<size_t, size_t>> get_section(const std::string &name) const;

private:
  int pid;
  size_t module_base;
  std::vector<MemRegion> cached_regions;
  int mem_file;
};