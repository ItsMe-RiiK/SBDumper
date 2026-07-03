# SBDumper

SBDumper is a memory dumper designed for Roblox (specifically targeting the Linux Sober environment). It safely reads the memory space of the target process to extract runtime class offsets, producing a clean, auto-generated C++ header file.

This project is a complete C++ port of the original Rust-based Turners Dumper.

## Features
- **Dynamic RTTI Scanning**: Reads live `/proc/[pid]/mem` using RTTI scanning and heuristics to find the true, up-to-date offsets at runtime.
- **Baseline Fallback**: Includes a hardcoded baseline of known offsets as a safety net in case of drastic memory layout changes.
- **Smart Offset Generation**: Automatically generates a structured `offsets.cpp` using nested C++ namespaces to prevent name collisions (e.g. `Camera::Address` vs `VisualEngine::Address`).
- **Status Stamps**: Every offset is intelligently tagged (e.g. `[CHANGED]`, `[NEW]`, `[SAME]`, `[BASELINE]`) to let you know if a game update moved an offset or if it's completely new.

## Getting Started

### Prerequisites
- Linux environment
- `cmake` (version 3.10+)
- A C++17 compatible compiler (`g++` or `clang++`)
- Root privileges (required for reading `/proc/[pid]/mem`)

### Building
Clone the repository and build the project using CMake:
```bash
git clone https://github.com/ItsMe-RiiK/SBDumper.git
cd SBDumper
mkdir build && cd build
cmake ..
make -j$(nproc)
```

*(Note: For IDE intellisense, you can generate compile commands by running `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..`)*

### Usage
Start the target process, then run the compiled dumper with elevated privileges:
```bash
sudo ./SoberDumper
```

The dumper will hook into the process, perform memory scanning, and output a freshly generated `offsets.cpp` inside the `build/offsets/` directory.

## Credits
- Original Rust Dumper logic and baseline by **Turnergamer** -
    https://github.com/Turnergamer/SoberDumper