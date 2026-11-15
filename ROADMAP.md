# Roadmap

Status key: ✅ done and tested · 🔶 partial · ⬜ not started

## Phase 0 — Discovery ✅
- ✅ Machine survey: macOS 15.5 arm64, clang, CMake, Python 3, OpenSCAD (`/opt/homebrew/bin/openscad`). Rust not installed.
- ✅ Reference version selected (26.3, REFERENCE_NOTES.md). Stack: C++20 + CMake + vendored sokol.

## Phase 1 — Cross-platform foundation ✅ (macOS verified; Windows compiles in CI, not yet run)
- ✅ CMake build, deterministic float flags, Metal (macOS) / D3D11 (Windows) selection.
- ✅ Logging, atomic/durable file I/O, platform data dirs, executable dir, process memory query, job system.
- ✅ Window, input, mouse lock, high-DPI, `.app` bundle with packed assets.
- ✅ CI for macOS arm64/x64 and Windows x64.

## Phase 2 — Rendering foundation ✅
- ✅ Original procedural 16×16 textures (`tools/gen_textures.py`), validating/deduplicating incremental packer (`tools/build_assets.py`).
- ✅ 2D texture array with alpha-coverage-preserving mips, nearest sampling.
- ✅ Section mesher: face culling, greedy merging, smooth light + AO, plants, fluids, small shapes. 12-byte packed vertices, shared index buffer.
- ✅ Hand-written MSL + HLSL shaders; opaque / cutout / translucent passes; reversed-Z infinite projection; frustum culling per section; fog; day/night light.
- ✅ Original 5×7 bitmap font, HUD, hotbar with isometric icons, block outline, F3 debug overlay.
- ⬜ Cave/occlusion culling, LOD for far terrain.

## Phase 3 — World and chunk foundation ✅
- ✅ Block registry, chunk data, modification tracking, deterministic generator v1, lighting, sparse journaled storage (Tests A–F), streaming.

## Phase 4 — Optimization foundation 🔶
- ✅ Background generation/lighting/meshing, chunk cache budget, storage cleanup, save validation, `vxworld`, `voxbench`, profiling overlay.
- ⬜ Mesh/texture VRAM budget enforcement, memory-pressure response.

## Phase 5 — Player gameplay 🔶
- ✅ Movement (walk, sprint, sneak with edge guard, jump, fly, swim, climb), exact swept AABB collision, raycast targeting, break/place/pick, player save.
- ⬜ Survival block breaking (hardness × tool), third-person camera.

## Phases 6–11 ⬜
Inventory/items/crafting, health/hunger, mobs and AI, fluids flow, circuits, dimensions, audio,
menus, particles, animation, packaging, stabilization (CLAUDE.md §25).

## Known limitations
- Windows: compiles in CI, but the D3D11 path (HLSL shaders compile at runtime) has not been run on a Windows machine.
- Generator golden hash pinned on arm64; CI checks x64.
- Creative-style instant breaking only; no items yet (hotbar holds blocks).
- Font has no descenders (g, p, q, y sit on the baseline).
