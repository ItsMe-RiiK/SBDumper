#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace stages
{
  static std::vector<size_t> collect_children(int fd, size_t addr)
  {
    std::vector<size_t> out;
    size_t cs = G_DUMPER.get_offset("Instance", "ChildrenStart").value_or(0);
    size_t ce = G_DUMPER.get_offset("Instance", "ChildrenEnd").value_or(0);
    if (cs == 0 || ce == 0)
      return out;

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

  static std::optional<std::string> read_sso(int fd, size_t addr)
  {
    auto size_byte = memory::read<uint8_t>(fd, addr);
    if (!size_byte)
      return std::nullopt;
    size_t len = *size_byte;

    if (len <= 15)
    {
      auto buf = memory::read_bytes(fd, addr + 1, 15);
      if (!buf)
        return std::nullopt;
      size_t end = 0;
      while (end < buf->size() && (*buf)[end] != 0)
        end++;
      if (end > len)
        end = len;
      std::string s(reinterpret_cast<const char *>(buf->data()), end);
      if (s.length() == len)
        return s;
      return std::nullopt;
    }
    else
    {
      auto ptr = memory::read<size_t>(fd, addr + 8);
      auto len2 = memory::read<size_t>(fd, addr + 16);
      if (!ptr || !len2 || *ptr < 0x10000 || *len2 > 256)
        return std::nullopt;
      return memory::read_string(fd, *ptr, *len2);
    }
  }

  static std::optional<size_t> find_service(int fd, size_t dm_addr, const std::string &class_name)
  {
    auto kids = collect_children(fd, dm_addr);
    for (size_t c : kids)
    {
      if (auto r = rtti::scan_rtti(fd, c))
      {
        if (r->name == class_name)
          return c;
      }
    }
    for (size_t off = 0; off < 0x4000; off += 8)
    {
      auto ptr = memory::read<size_t>(fd, dm_addr + off);
      if (!ptr || *ptr < 0x10000)
        continue;
      if (auto r = rtti::scan_rtti(fd, *ptr))
      {
        if (r->name == class_name)
          return *ptr;
      }
    }
    return std::nullopt;
  }

  static void dump_sound_service(int fd, size_t svc)
  {
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, svc + off))
      {
        if (*v >= 0.0f && *v <= 2.0f)
        {
          G_DUMPER.add_offset("SoundService", "Volume", off);
          break;
        }
      }
    }
  }

  static void dump_user_input_service(int fd, size_t svc)
  {
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read<uint32_t>(fd, svc + off))
      {
        if (*v >= 1 && *v <= 3)
        {
          auto next = memory::read<uint32_t>(fd, svc + off + 4).value_or(99);
          if (next > 10)
          {
            G_DUMPER.add_offset("UserInputService", "MouseBehavior", off);
            break;
          }
        }
      }
    }
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, svc + off))
      {
        if (std::abs(*v - 1.0f) < 0.1f && *v > 0.0f)
        {
          G_DUMPER.add_offset("UserInputService", "MouseDeltaSensitivity", off);
          break;
        }
      }
    }
    for (size_t off = 0; off < 0x100; off += 1)
    {
      if (auto v = memory::read<uint8_t>(fd, svc + off))
      {
        if (*v == 1)
        {
          const char *off_name = nullptr;
          if (!G_DUMPER.get_offset("UserInputService", "MouseEnabled"))
          {
            off_name = "MouseEnabled";
          }
          else if (!G_DUMPER.get_offset("UserInputService", "KeyboardEnabled"))
          {
            off_name = "KeyboardEnabled";
          }
          else if (!G_DUMPER.get_offset("UserInputService", "TouchEnabled"))
          {
            off_name = "TouchEnabled";
          }
          else
          {
            continue;
          }
          G_DUMPER.add_offset("UserInputService", off_name, off);
        }
      }
    }
  }

  static void dump_core_gui(int fd, size_t svc)
  {
    auto kids = collect_children(fd, svc);
    for (size_t k : kids)
    {
      if (auto r = rtti::scan_rtti(fd, k))
      {
        if (r->name == "ScreenGui@RBX")
        {
          for (size_t off = 0; off < 0x200; off += 8)
          {
            if (auto ptr = memory::read<size_t>(fd, k + off))
            {
              if (*ptr >= 0x10000)
              {
                if (auto s = memory::read_name_fmt(fd, *ptr))
                {
                  if (!s->empty() && s->length() < 60)
                  {
                    std::string ns = "ScreenGui_" + *s;
                    G_DUMPER.add_offset(ns, "Address", k);
                    break;
                  }
                }
              }
            }
            if (auto s = read_sso(fd, k + off))
            {
              if (!s->empty() && s->length() < 60)
              {
                std::string ns = "ScreenGui_" + *s;
                G_DUMPER.add_offset(ns, "Address", k);
                break;
              }
            }
          }
        }
      }
    }
  }

  static void dump_network_client(int fd, size_t svc)
  {
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read<uint32_t>(fd, svc + off))
      {
        if (*v <= 4)
        {
          G_DUMPER.add_offset("NetworkClient", "ConnectionState", off);
          break;
        }
      }
    }
    for (size_t off = 0; off < 0x300; off += 8)
    {
      if (auto ptr = memory::read<size_t>(fd, svc + off))
      {
        if (*ptr >= 0x10000)
        {
          if (auto s = memory::read_name_fmt(fd, *ptr))
          {
            if (s->length() > 30 && (s->find('-') != std::string::npos || s->find('_') != std::string::npos))
            {
              G_DUMPER.add_offset("NetworkClient", "Ticket", off);
              break;
            }
          }
        }
      }
    }
  }

  static void dump_run_service(int fd, size_t svc)
  {
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, svc + off))
      {
        if (*v > 0.005f && *v < 0.1f)
        {
          G_DUMPER.add_offset("RunService", "HeartbeatDelta", off);
          break;
        }
      }
    }
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, svc + off))
      {
        if (*v > 0.005f && *v < 0.1f)
        {
          size_t hb = G_DUMPER.get_offset("RunService", "HeartbeatDelta").value_or(-1);
          if (off != hb)
          {
            G_DUMPER.add_offset("RunService", "RenderSteppedDelta", off);
            break;
          }
        }
      }
    }
    for (size_t off = 0; off < 0x200; off += 4)
    {
      if (auto v = memory::read_f32(fd, svc + off))
      {
        if (std::abs(*v - 240.0f) < 10.0f)
        {
          G_DUMPER.add_offset("RunService", "SimulationRate", off);
          break;
        }
      }
    }
  }

  static void dump_replicated_storage(int fd, size_t svc)
  {
    auto kids = collect_children(fd, svc);
    std::cerr << "  ReplicatedStorage has " << kids.size() << " child(ren)\n";
  }

  static void dump_script_context(int fd, size_t svc)
  {
    for (size_t off = 0; off < 0x500; off += 8)
    {
      if (auto ptr = memory::read<size_t>(fd, svc + off))
      {
        if (*ptr >= 0x10000)
        {
          if (auto r = rtti::scan_rtti(fd, *ptr))
          {
            if (r->name.find("Script") != std::string::npos || r->name.find("ExecutionContext") != std::string::npos ||
                r->name.find("Vm") != std::string::npos)
            {
              std::string base_name = r->name.substr(0, r->name.find('@'));
              G_DUMPER.add_offset("ScriptContext", base_name + "Ref", off);
              break;
            }
          }
        }
      }
    }
    for (size_t off = 0; off < 0x200; off += 8)
    {
      if (auto v = memory::read<size_t>(fd, svc + off))
      {
        if (*v > 100000000 && *v < 1000000000000ULL)
        {
          G_DUMPER.add_offset("ScriptContext", "ExtraMemory", off);
          break;
        }
      }
    }
  }

  void services_extra(int fd)
  {
    std::cerr << "[services_extra]\n";

    size_t dm_addr = G_DATA_MODEL_ADDR;

    using DumperFn = void (*)(int, size_t);

    struct ServiceTodo
    {
      const char *rtti_name;
      DumperFn fn;
    };

    ServiceTodo service_todos[] = {
      { "SoundService@RBX", dump_sound_service },
      { "UserInputService@RBX", dump_user_input_service },
      { "CoreGui@RBX", dump_core_gui },
      { "NetworkClient@RBX", dump_network_client },
      { "RunService@RBX", dump_run_service },
      { "ReplicatedStorage@RBX", dump_replicated_storage },
      { "ScriptContext@RBX", dump_script_context },
    };

    for (const auto &todo : service_todos)
    {
      if (auto addr = find_service(fd, dm_addr, todo.rtti_name))
      {
        std::string ns = std::string(todo.rtti_name);
        ns = ns.substr(0, ns.find('@'));
        std::cerr << "  " << ns << " @ 0x" << std::hex << *addr << std::dec << "\n";
        G_DUMPER.add_offset(ns, "Address", *addr);
        todo.fn(fd, *addr);
      }
    }

    const char *extra_services[] = { "ReplicatedFirst@RBX",
                                     "ServerScriptService@RBX",
                                     "ServerStorage@RBX",
                                     "StarterGui@RBX",
                                     "StarterPack@RBX",
                                     "StarterPlayer@RBX",
                                     "HttpService@RBX",
                                     "LogService@RBX",
                                     "Chat@RBX",
                                     "InsertService@RBX",
                                     "ContentProvider@RBX",
                                     "Debris@RBX",
                                     "CollectionService@RBX",
                                     "PhysicsService@RBX",
                                     "JointsService@RBX",
                                     "TweenService@RBX",
                                     "Selection@RBX",
                                     "GuiService@RBX",
                                     "Teams@RBX",
                                     "BadgeService@RBX",
                                     "SocialService@RBX",
                                     "MarketplaceService@RBX",
                                     "TeleportService@RBX",
                                     "PresenceService@RBX",
                                     "LocalizationService@RBX",
                                     "PolicyService@RBX",
                                     "FriendService@RBX",
                                     "GroupService@RBX",
                                     "SuggestionsService@RBX",
                                     "PermissionsService@RBX",
                                     "RobloxReplicatedStorage@RBX",
                                     "RbxAnalyticsService@RBX",
                                     "AnalyticsService@RBX",
                                     "AdService@RBX",
                                     "VRService@RBX",
                                     "VoiceService@RBX",
                                     "TextChatService@RBX",
                                     "GamepadService@RBX",
                                     "KeyframeSequenceProvider@RBX",
                                     "AnimationClipProvider@RBX",
                                     "MaterialService@RBX",
                                     "TerrainRegion@RBX" };

    for (const char *rtti_name : extra_services)
    {
      std::string ns = std::string(rtti_name);
      ns = ns.substr(0, ns.find('@'));
      if (G_DUMPER.get_offset(ns, "Address"))
        continue;
      if (auto addr = find_service(fd, dm_addr, rtti_name))
      {
        std::cerr << "  " << ns << " @ 0x" << std::hex << *addr << std::dec << "\n";
        G_DUMPER.add_offset(ns, "Address", *addr);
      }
    }
  }
} // namespace stages
