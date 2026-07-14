#pragma once
#include "dumper.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace writer
{
  void write_offsets(const std::map<std::string, std::vector<OffsetEntry>> &offsets, uint64_t elapsed_ms);
}
