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

  void sound(int fd)
  {
    std::cerr << "[sound]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;

    auto sounds = find_instances(fd, ws_addr, "Sound@RBX", 3);
    if (sounds.empty())
    {
      for (size_t off = 0; off < 0x2000; off += 8)
      {
        if (auto ptr = memory::read<size_t>(fd, ws_addr + off))
        {
          if (*ptr >= 0x10000)
          {
            if (auto r = rtti::scan_rtti(fd, *ptr))
            {
              if (r->name == "Sound@RBX")
              {
                sounds.push_back(*ptr);
                break;
              }
            }
          }
        }
      }
    }

    if (sounds.empty())
    {
      std::cerr << "  No Sound found\n";
      return;
    }
    std::cerr << "  Found " << sounds.size() << " Sound(s)\n";
    size_t s = sounds[0];

    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, s + off))
      {
        if (std::abs(*v - 1.0f) < 0.05f && *v >= 0.0f && *v <= 10.0f)
        {
          G_DUMPER.add_offset("Sound", "Volume", off);
          std::cerr << "  Sound::Volume at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, s + off))
      {
        if (std::abs(*v - 1.0f) < 0.05f && *v >= 0.0f && *v <= 10.0f)
        {
          size_t vol_off = G_DUMPER.get_offset("Sound", "Volume").value_or(-1);
          if (off == vol_off)
            continue;
          G_DUMPER.add_offset("Sound", "Pitch", off);
          std::cerr << "  Sound::Pitch at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x300; off += 8)
    {
      if (auto ptr = memory::read<size_t>(fd, s + off))
      {
        if (*ptr >= 0x10000)
        {
          if (auto sid = memory::read_name_fmt(fd, *ptr))
          {
            if (sid->find("rbxasset://") == 0 || sid->find("http") == 0)
            {
              G_DUMPER.add_offset("Sound", "SoundId", off);
              std::cerr << "  Sound::SoundId at +0x" << std::hex << off << std::dec << "\n";
              break;
            }
          }
        }
      }
    }

    for (size_t off = 0; off < 0x100; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, s + off))
      {
        if (*v != 0 && *v != 1)
          continue;
        uint8_t next = memory::read<uint8_t>(fd, s + off + 1).value_or(2);
        if ((*v == 0 && next == 1) || (*v == 1 && next <= 1))
        {
          G_DUMPER.add_offset("Sound", "Looped", off);
          std::cerr << "  Sound::Looped at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    {
      auto skip_vol = G_DUMPER.get_offset("Sound", "Volume");
      auto skip_pit = G_DUMPER.get_offset("Sound", "Pitch");
      for (size_t off = 0; off < 0x200; off += 4)
      {
        if ((skip_vol && *skip_vol == off) || (skip_pit && *skip_pit == off))
          continue;
        if (auto v = memory::read_f32(fd, s + off))
        {
          if (std::abs(*v - 1.0f) < 0.05f && *v >= 0.0f && *v <= 10.0f)
          {
            G_DUMPER.add_offset("Sound", "PlaybackSpeed", off);
            std::cerr << "  Sound::PlaybackSpeed at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }

    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, s + off))
      {
        if (std::abs(*v) > 0.01f)
          continue;
        size_t vol = G_DUMPER.get_offset("Sound", "Volume").value_or(-1);
        size_t pit = G_DUMPER.get_offset("Sound", "Pitch").value_or(-1);
        size_t spd = G_DUMPER.get_offset("Sound", "PlaybackSpeed").value_or(-1);
        if (off == vol || off == pit || off == spd)
          continue;
        G_DUMPER.add_offset("Sound", "TimePosition", off);
        std::cerr << "  Sound::TimePosition at +0x" << std::hex << off << std::dec << "\n";
        break;
      }
    }

    size_t looped_off = G_DUMPER.get_offset("Sound", "Looped").value_or(-1);
    for (size_t off = 0; off < 0x100; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, s + off))
      {
        if (*v != 0 && *v != 1)
          continue;
        if (std::abs((long long)off - (long long)looped_off) <= 1)
          continue;
        if (auto next = memory::read<uint8_t>(fd, s + off + 1))
        {
          if (*next == 0 || *next == 1)
          {
            G_DUMPER.add_offset("Sound", "PlayOnRemove", off);
            std::cerr << "  Sound::PlayOnRemove at +0x" << std::hex << off << std::dec << "\n";
            break;
          }
        }
      }
    }
  }
} // namespace stages
