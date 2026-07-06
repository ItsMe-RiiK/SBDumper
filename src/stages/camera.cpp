#include "dumper.hpp"
#include "memory.hpp"
#include "rtti.hpp"
#include "stages.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>

namespace stages
{
  bool camera(int fd)
  {
    std::cerr << "[camera]\n";

    size_t ws_addr = G_WORKSPACE_ADDR;
    auto cc_off = G_DUMPER.get_offset("Workspace", "CurrentCamera");
    if (!cc_off)
    {
      std::cerr << "No CurrentCamera offset\n";
      return false;
    }
    auto cam_addr_opt = memory::read<size_t>(fd, ws_addr + *cc_off);
    if (!cam_addr_opt)
    {
      std::cerr << "Failed to read Camera addr\n";
      return false;
    }
    size_t cam_addr = *cam_addr_opt;
    std::cerr << "  Camera @ 0x" << std::hex << cam_addr << std::dec << "\n";

    bool cframe_found = false;
    for (size_t off = 0; off < 0x300; off += 4)
    {
      auto buf = memory::read_bytes(fd, cam_addr + off, 48);
      if (!buf)
        continue;

      const float *f = reinterpret_cast<const float *>(buf->data());
      bool has_nan = false;
      for (int i = 0; i < 12; ++i)
      {
        if (std::isnan(f[i]) || std::isinf(f[i]))
        {
          has_nan = true;
          break;
        }
      }
      if (has_nan)
        continue;

      float axes[3][3] = { { f[0], f[1], f[2] }, { f[3], f[4], f[5] }, { f[6], f[7], f[8] } };
      bool ortho = true;
      for (int i = 0; i < 3; ++i)
      {
        float len = std::sqrt(axes[i][0] * axes[i][0] + axes[i][1] * axes[i][1] + axes[i][2] * axes[i][2]);
        if (std::abs(len - 1.0f) > 0.01f || std::abs(axes[i][0]) > 1.5f || std::abs(axes[i][1]) > 1.5f || std::abs(axes[i][2]) > 1.5f)
        {
          ortho = false;
          break;
        }
      }
      if (!ortho)
        continue;

      float dot01 = axes[0][0] * axes[1][0] + axes[0][1] * axes[1][1] + axes[0][2] * axes[1][2];
      float dot02 = axes[0][0] * axes[2][0] + axes[0][1] * axes[2][1] + axes[0][2] * axes[2][2];
      if (std::abs(dot01) > 0.01f || std::abs(dot02) > 0.01f)
        continue;

      float det = axes[0][0] * (axes[1][1] * axes[2][2] - axes[1][2] * axes[2][1]) -
                  axes[0][1] * (axes[1][0] * axes[2][2] - axes[1][2] * axes[2][0]) +
                  axes[0][2] * (axes[1][0] * axes[2][1] - axes[1][1] * axes[2][0]);
      if (std::abs(det - 1.0f) > 0.02f)
        continue;

      float pos[3] = { f[9], f[10], f[11] };
      if (std::abs(pos[0]) > 1e8f || std::abs(pos[1]) > 1e8f || std::abs(pos[2]) > 1e8f)
        continue;

      G_DUMPER.add_offset("Camera", "CFrame", off);
      G_DUMPER.add_offset("Camera", "Position", off + 0x24);
      G_DUMPER.add_offset("Camera", "Rotation", off);
      std::cerr << "  CFrame at +0x" << std::hex << off << std::dec << "\n";
      cframe_found = true;
      break;
    }
    if (!cframe_found)
      return false;

    for (size_t off = 0; off < 0x200; off += 8)
    {
      auto ptr = memory::read<size_t>(fd, cam_addr + off);
      if (!ptr || *ptr < 0x10000)
        continue;
      if (auto rtti = rtti::scan_rtti(fd, *ptr))
      {
        if (rtti->name == "Humanoid@RBX")
        {
          G_DUMPER.add_offset("Camera", "CameraSubject", off);
          std::cerr << "  CameraSubject at +0x" << std::hex << off << std::dec << "\n";
          break;
        }
      }
    }

    for (size_t off = 0; off < 0x100; off += 4)
    {
      auto v = memory::read<uint32_t>(fd, cam_addr + off);
      if (!v)
        continue;
      if (*v == 1)
      {
        auto next = memory::read<uint32_t>(fd, cam_addr + off + 4).value_or(99);
        if (next > 10)
        {
          G_DUMPER.add_offset("Camera", "CameraType", off);
          break;
        }
      }
    }

    const float common_res[][2] = { { 1920.0f, 1080.0f }, { 1366.0f, 768.0f }, { 2560.0f, 1440.0f }, { 3840.0f, 2160.0f },
                                    { 1440.0f, 900.0f },  { 1536.0f, 864.0f }, { 1280.0f, 720.0f },  { 1680.0f, 1050.0f } };

    for (size_t off = 0; off < 0x500; off += 4)
    {
      auto v = memory::read<std::array<float, 2>>(fd, cam_addr + off);
      if (!v)
        continue;
      if (std::isnan((*v)[0]) || std::isnan((*v)[1]) || std::isinf((*v)[0]) || std::isinf((*v)[1]))
        continue;
      if (std::fpclassify((*v)[0]) == FP_SUBNORMAL || std::fpclassify((*v)[1]) == FP_SUBNORMAL)
        continue;
      if ((*v)[0] < 100.0f || (*v)[1] < 100.0f || (*v)[0] > 10000.0f || (*v)[1] > 10000.0f)
        continue;

      float aspect = (*v)[0] / (*v)[1];
      if (aspect < 0.5f || aspect > 3.0f)
        continue;

      bool matched = false;
      for (const auto &res : common_res)
      {
        if (std::abs((*v)[0] - res[0]) < 10.0f && std::abs((*v)[1] - res[1]) < 10.0f)
        {
          matched = true;
          break;
        }
        if (std::abs((*v)[0] - res[1]) < 10.0f && std::abs((*v)[1] - res[0]) < 10.0f)
        {
          matched = true;
          break;
        }
      }
      if (matched || ((*v)[0] > 100.0f && (*v)[0] < 10000.0f && aspect > 0.5f && aspect < 3.0f))
      {
        G_DUMPER.add_offset("Camera", "ViewportSize", off);
        std::cerr << "  ViewportSize at +0x" << std::hex << off << std::dec << " (" << std::fixed << std::setprecision(0) << (*v)[0] << "x"
                  << (*v)[1] << ")\n";
        break;
      }
    }

    if (!G_DUMPER.get_offset("Camera", "ViewportSize"))
    {
      for (size_t off = 0; off < 0x500; off += 2)
      {
        auto x = memory::read<int16_t>(fd, cam_addr + off);
        auto y = memory::read<int16_t>(fd, cam_addr + off + 2);
        if (!x || !y)
          continue;
        if (*x > 100 && *y > 100 && *x < 10000 && *y < 10000)
        {
          float aspect = static_cast<float>(*x) / static_cast<float>(*y);
          if (aspect > 0.5f && aspect < 3.0f)
          {
            G_DUMPER.add_offset("Camera", "ViewportSize", off);
            std::cerr << "  ViewportSize at +0x" << std::hex << off << std::dec << " (int16: " << *x << "x" << *y << ")\n";
            break;
          }
        }
      }
    }

    return true;
  }
} // namespace stages
