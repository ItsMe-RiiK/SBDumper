#include "process.hpp"

#include <algorithm>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

static std::vector<MemRegion> parse_maps(int pid)
{
  std::vector<MemRegion> regions;
  std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
  std::ifstream file(maps_path);
  if (!file.is_open())
    return regions;

  std::string line;
  while (std::getline(file, line))
  {
    std::istringstream iss(line);
    std::string addr_range, perms, offset, dev, inode;
    std::string path;

    if (!(iss >> addr_range >> perms >> offset >> dev >> inode))
      continue;
    std::getline(iss >> std::ws, path);

    size_t dash = addr_range.find('-');
    if (dash == std::string::npos)
      continue;

    size_t start = std::stoull(addr_range.substr(0, dash), nullptr, 16);
    size_t end = std::stoull(addr_range.substr(dash + 1), nullptr, 16);

    regions.push_back({ start, end, perms, path });
  }
  return regions;
}

static std::optional<int> find_process_by_name(const std::string &name)
{
  for (const auto &entry : fs::directory_iterator("/proc"))
  {
    if (!entry.is_directory())
      continue;
    std::string filename = entry.path().filename().string();
    if (filename.empty() || !std::isdigit(filename[0]))
      continue;

    int pid = std::stoi(filename);
    std::string comm_path = entry.path().string() + "/comm";
    std::ifstream comm_file(comm_path);
    if (comm_file.is_open())
    {
      std::string comm;
      std::getline(comm_file, comm);
      // Trim whitespace
      comm.erase(comm.find_last_not_of(" \n\r\t") + 1);
      if (comm == name)
      {
        return pid;
      }
    }
  }
  return std::nullopt;
}

static std::optional<size_t> find_libroblox_base(int pid)
{
  auto maps = parse_maps(pid);

  struct Candidate
  {
    size_t addr;
    size_t size;
    std::string path;
  };

  std::vector<Candidate> candidates;

  for (const auto &r : maps)
  {
    if (r.path.find("memfd") == std::string::npos && r.path.find("(deleted)") == std::string::npos)
      continue;
    if (r.perms.find('x') == std::string::npos)
      continue;
    size_t size = r.end - r.start;
    if (size > 50 * 1024 * 1024)
    {
      candidates.push_back({ r.start, size, r.path });
    }
  }

  if (candidates.empty())
    return std::nullopt;

  std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
  std::ifstream file(maps_path);
  if (file.is_open())
  {
    std::string line;
    while (std::getline(file, line))
    {
      std::istringstream iss(line);
      std::string addr_range, perms, offset_str, dev, inode, path;
      if (!(iss >> addr_range >> perms >> offset_str >> dev >> inode))
        continue;
      std::getline(iss >> std::ws, path);

      if (path.find("memfd") == std::string::npos && path.find("(deleted)") == std::string::npos)
        continue;
      if (perms.find('x') == std::string::npos)
        continue;

      size_t dash = addr_range.find('-');
      if (dash == std::string::npos)
        continue;

      size_t start = std::stoull(addr_range.substr(0, dash), nullptr, 16);
      size_t offset = std::stoull(offset_str, nullptr, 16);
      size_t end = std::stoull(addr_range.substr(dash + 1), nullptr, 16);
      size_t size = end - start;

      if (size > 50 * 1024 * 1024 && offset == 0)
      {
        return start;
      }
    }
  }

  std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.addr < b.addr; });
  return candidates.front().addr;
}

Process::Process() : pid(0), module_base(0), mem_file(-1) {}

Process::~Process()
{
  if (mem_file != -1)
  {
    close(mem_file);
  }
}

bool Process::attach(const std::string &name)
{
  auto pid_opt = find_process_by_name(name);
  if (!pid_opt)
  {
    std::cerr << "Failed to find process: " << name << std::endl;
    return false;
  }
  this->pid = *pid_opt;

  auto base_opt = find_libroblox_base(this->pid);
  if (!base_opt)
  {
    std::cerr << "Failed to find libroblox.so base" << std::endl;
    return false;
  }
  this->module_base = *base_opt;
  this->cached_regions = parse_maps(this->pid);

  std::string mem_path = "/proc/" + std::to_string(this->pid) + "/mem";
  this->mem_file = open(mem_path.c_str(), O_RDONLY);
  if (this->mem_file == -1)
  {
    std::cerr << "Failed to open " << mem_path << std::endl;
    std::cerr << "Try running with sudo (the Main process is owned by root)" << std::endl;
    return false;
  }

  std::cout << "Attached to PID: " << this->pid << ", module base: 0x" << std::hex << this->module_base << std::dec << std::endl;
  return true;
}

int Process::get_pid() const
{
  return pid;
}

size_t Process::get_module_base() const
{
  return module_base;
}

int Process::get_mem_file() const
{
  return mem_file;
}

std::optional<std::pair<size_t, size_t>> Process::get_section(const std::string &name) const
{
  if (module_base == 0)
    return std::nullopt;

  size_t mod_end = module_base + 0x10000000;
  std::vector<std::pair<size_t, size_t>> matching;

  for (const auto &r : cached_regions)
  {
    if (r.start < module_base || r.start >= mod_end)
      continue;

    bool match_perm = false;
    if (name == ".data")
    {
      match_perm = (r.perms == "rw-p" || r.perms == "r--p");
    }
    else if (name == ".rdata" || name == ".rodata")
    {
      match_perm = (r.perms == "r--p");
    }
    else if (name == ".text")
    {
      match_perm = (r.perms.find('x') != std::string::npos);
    }

    if (match_perm)
    {
      matching.push_back({ r.start, r.end });
    }
  }

  if (matching.empty())
    return std::nullopt;

  std::sort(matching.begin(), matching.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  size_t region_start = matching[0].first;
  size_t region_end = matching[0].second;

  for (size_t i = 1; i < matching.size(); ++i)
  {
    if (matching[i].first <= region_end + 0x1000)
    {
      region_end = std::max(region_end, matching[i].second);
    }
    else
    {
      region_end = matching[i].second;
    }
  }

  return std::make_pair(region_start, region_end - region_start);
}
