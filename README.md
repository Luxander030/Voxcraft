# Voxcraft

An original block-building sandbox game with its own engine, written in C++20. It targets Windows
and macOS from one codebase.

**Status:** early development. The headless engine core is done and tested: world generation,
lighting, chunk streaming, and sparse journaled saves. The windowed game, renderer and gameplay
are next. See [ROADMAP.md](ROADMAP.md).

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
- Python 3 (asset pipeline)
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
