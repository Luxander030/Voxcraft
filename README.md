# Voxcraft

An original block-building sandbox game with its own engine, written in C++20. It targets Windows
and macOS from one codebase.

**Status:** early development. You can explore an endless generated world, fly, walk, swim, and
break and place blocks. Saves are sparse and crash-safe. Survival, items, crafting and creatures
are next. See [ROADMAP.md](ROADMAP.md).

## Run

```sh
open build/Voxcraft.app                       # macOS
build\Release\voxcraft.exe                    # Windows
./build/Voxcraft.app/Contents/MacOS/Voxcraft --world MyWorld --seed 42 --radius 12
```

| Control | Action |
|---|---|
| WASD, mouse | move, look (click to capture the mouse) |
| Space / Left Shift | jump (fly up) / sneak (fly down) |
| Left Ctrl or R | sprint |
| F | toggle flying |
| Left / Right / Middle click | break / place / pick block |
| 1–9, wheel | hotbar |
| F3 | debug overlay |
| F5 | save now |
| F11 | fullscreen |
| Esc | pause; Esc again saves and quits |

Worlds are stored in `~/Library/Application Support/Voxcraft/saves` on macOS and
`%APPDATA%\Voxcraft\saves` on Windows. The world is also autosaved every 45 s.

## Highlights
- Deterministic, versioned world generation that is bit-identical across x64 and arm64.
- Saves store only what you changed. An untouched chunk costs 0 bytes on disk.
- Crash-safe, journaled multi-file saves with automatic recovery.
- `vxworld` maintenance tool: `info`, `verify`, and `optimize` (dry run by default, backs up
  before changes).

## Supported platforms
- macOS 11+ (Apple Silicon and Intel), Metal
- Windows 10/11 x64, Direct3D 11

## Requirements
- CMake ≥ 3.20
- A C++20 compiler: Xcode command-line tools (clang) or Visual Studio 2022
- Python 3 (asset pipeline, standard library only)
- OpenSCAD (model pipeline)

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/voxtests        # unit + storage tests
./build/voxbench        # core benchmarks
./build/vxworld info <world-dir>
```

On Windows, use `cmake --build build --config Release` and run `build\Release\voxtests.exe`.

## Documentation
- [ARCHITECTURE.md](ARCHITECTURE.md)
- [PERFORMANCE.md](PERFORMANCE.md)
- [docs/WORLD_FORMAT.md](docs/WORLD_FORMAT.md)
- [ASSET_PIPELINE.md](ASSET_PIPELINE.md)
- [REFERENCE_NOTES.md](REFERENCE_NOTES.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)
- [THIRD_PARTY.md](THIRD_PARTY.md)
