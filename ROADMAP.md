# Roadmap

Status key: ✅ done and tested · 🔶 partial · ⬜ not started

## Phase 0 — Discovery ✅
- ✅ Machine survey: macOS 15.5, arm64, clang, CMake, Python 3, OpenSCAD (`/opt/homebrew/bin/openscad`). Rust not installed.
- ✅ Reference version selected (26.3, see REFERENCE_NOTES.md).
- ✅ Stack chosen: C++20 + CMake + vendored sokol (ARCHITECTURE.md).

## Phase 1 — Cross-platform foundation 🔶
- ✅ CMake build, warnings, deterministic float flags, Metal/D3D11 selection.
- ✅ Logging, atomic/durable file I/O, platform data dirs, job system.
- ⬜ Window + input + GPU init (`src/app`, `src/platform/sokol_impl.*`).
- 🔶 CI workflow for Windows + macOS (headless core + tests). Game packaging comes later.

## Phase 2 — Rendering foundation ⬜
- ⬜ Camera, section meshing (culling, greedy), shaders, texture array, passes, frustum culling.

## Phase 3 — World and chunk foundation ✅ (headless)
- ✅ Data-driven block registry with flattened block states (`assets/data/blocks.json`).
- ✅ Chunk data: uniform/copy-on-write sections, exact modification tracking.
- ✅ Deterministic generator v1: biomes, terrain, caves, ores, trees, cross-chunk features.
- ✅ Lighting from 3×3 snapshots.
- ✅ Sparse records, region files, journaled saves, recovery. Storage Tests A–F pass.
- ✅ Streaming with hysteresis, pending write-back set, clean LRU cache.

## Phase 4 — Optimization foundation 🔶
- ✅ Background generation and lighting, chunk cache with byte budget, storage cleanup, save validation, `vxworld` CLI, `voxbench`.
- ⬜ Profiling overlay (needs the renderer). ⬜ Mesh/texture memory budgets.

## Phases 5–11 ⬜
Player physics and interaction, inventory/items/crafting, mobs and AI, fluids, circuits,
dimensions, audio, UI, particles, animation, packaging, stabilization. See CLAUDE.md §25 for the
full list. Each becomes ✅ only with tests.

## Known limitations
- No graphical build yet: the game target is skipped until `src/app/main.cpp` exists.
- The Windows build has not been run yet. CI is the first check of it.
- The generator golden hash is pinned on arm64 only, until CI confirms x64.
