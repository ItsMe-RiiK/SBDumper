#include "writer.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <sstream>

namespace fs = std::filesystem;

namespace writer
{
  static fs::path offsets_dir()
  {
    return fs::current_path() / "offsets";
  }

  static std::string get_val(const std::string &ns, const std::string &name)
  {
    if (auto v = G_DUMPER.get_value(ns, name))
    {
      return " // " + std::to_string(*v);
    }
    return "";
  }

  static std::string build_header(size_t count, uint64_t elapsed_ms)
  {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    return "/*  ===========================\n"
           "*   RIIK's Dumper\n"
           "*   ===========================\n"
           "*   " +
           std::to_string(count) + " offsets in " + std::to_string(elapsed_ms) +
           "ms\n"
           "*   [auto-generated]\n"
           "*   generated at " +
           oss.str() +
           "\n*/"
           "\n\n";
  }

  static void write_cpp(const std::map<std::string, std::vector<OffsetEntry>> &offsets, const std::string &header)
  {
    std::string out = header + "#include <cstdint>\n\nnamespace offsets {\n";
    for (const auto &[ns, entries] : offsets)
    {
      out += "    namespace " + ns + " {\n";
      for (const auto &e : entries)
      {
        std::string v = get_val(ns, e.name);

        std::string st_str;
        switch (e.status)
        {
        case OffsetStatus::BASELINE:
          st_str = " // [BASELINE]";
          break;
        case OffsetStatus::CHANGED:
          st_str = " // [CHANGED]";
          break;
        case OffsetStatus::UNCHANGED:
          st_str = " // [SAME]";
          break;
        case OffsetStatus::NEW:
          st_str = " // [NEW]";
          break;
        }

        char buf[256];
        snprintf(buf, sizeof(buf), "        constexpr std::uintptr_t %s = 0x%zX;%s%s\n", e.name.c_str(), e.offset, st_str.c_str(),
                 v.c_str());
        out += buf;
      }
      out += "    }\n\n";
    }
    out += "} // namespace offsets\n";

    std::ofstream f(offsets_dir() / "offsets.cpp");
    if (f.is_open())
    {
      f << out;
    }
  }

  void write_offsets(const std::map<std::string, std::vector<OffsetEntry>> &offsets, uint64_t elapsed_ms)
  {
    fs::create_directories(offsets_dir());

    size_t count = 0;
    for (const auto &[ns, entries] : offsets)
    {
      count += entries.size();
    }

    std::string header = build_header(count, elapsed_ms);

    write_cpp(offsets, header);

    auto od = offsets_dir();
    std::cout << "Found " << count << " offsets in " << elapsed_ms << "ms\n";
    std::cout << "Written to " << (od / "offsets.cpp").string() << std::endl;
  }
} // namespace writer
