# Architecture

## Technology stack (decided)

| Concern | Choice | Why |
|---|---|---|
| Language | C++20 | Installed toolchains on both platforms (clang / MSVC). Full control over memory layout. Rust + wgpu was the other candidate but needs an install that has not been approved; nothing below depends on that decision. |
| Build | CMake ≥ 3.20 | One build for Xcode/Makefiles/Ninja/Visual Studio |
| Window, input, GPU | Vendored `sokol_app` + `sokol_gfx` (zlib licence, header-only) | Thin layer over Metal (macOS) and D3D11 (Windows). The renderer itself (meshing, atlases, culling, passes) is our own code. |
| Audio | `sokol_audio` (vendored) + our own mixer | Stream callback only; mixing and 3D attenuation are in-house |
| Compression | LZ4 block (vendored, BSD-2) | ~GB/s decode, which keeps loading off the critical path |
| Images | `stb_image` (public domain) | PNG decode in the asset tool only; the game loads pre-packed atlases |
| Physics, serialization, JSON, job system, noise | In-house | Small and specific to voxels; avoids dependencies |

## Module layout

```
src/core      platform-free basics: types/math, logging, binary I/O, CRC, LZ4 wrappers, atomic file
              writes, JSON, job system, noise, profiler counters
src/world     block registry, chunk data, world generation, lighting, World (lifecycle + streaming)
src/storage   region files, sparse chunk records, save journal, maintenance (optimize/verify)
src/render    CPU side of rendering (meshing, atlas layout) - testable without a GPU   [next]
src/game      player, physics, inventory, items, crafting, entities, block behaviours    [next]
src/app       sokol app entry, GPU backend glue, UI                                       [next]
src/platform  sokol implementation units (Metal on macOS, D3D11 on Windows)
src/tools     vxworld maintenance CLI
tools/        asset pipeline scripts (texture packer, OpenSCAD export)
```

Dependency direction: `app → render → game → world → storage → core`. Everything below `app` runs
headless, which is what `voxtests` and `voxbench` link against.

## Chunk lifecycle

`Requested → Generating → Generated → Ready (lit) → [unsaved] → Unloading → Cached | gone`

- `World::update()` (main thread) requests every chunk within `loadRadius` and unloads beyond
  `loadRadius + 2` (hysteresis).
- Generation runs on the `JobSystem`. A job's priority is the squared distance to the *current*
  focus, re-evaluated each time a worker picks a job, so stale far-away requests sink. Results
  come back through `postMain`; a cancel token plus slot identity check drops results that arrive
  for chunks unloaded in the meantime. One slot per position means a chunk is never generated
  twice at once.
- Lighting needs the 3×3 neighbourhood to be `Generated`. It runs as a pure function of
  copy-on-write section snapshots (no locks).
- Unloading: if the chunk has unsaved persistent data, its record is encoded right away into
  `pending_` (an in-memory write-back set that is read *before* disk). Clean chunks go to a
  byte-bounded LRU cache (`MemoryBudget::chunkCacheBytes`). Evicting from the cache never loses
  data, because cached chunks are clean by construction.

## Deterministic world generation

`WorldGenerator::generate(chunk)` is a pure function of *(generator version, seed, settings,
dimension, chunk position)*.

- Noise is integer-hashed gradient noise using only `+ - * /` and floor. There are no lookup tables
  seeded from `rand` and no `sin`/`exp` calls.
- The build sets `-ffp-contract=off` (clang/gcc) and `/fp:precise` (MSVC) so x64 and arm64 produce
  bit-identical floats.
- Features that cross chunk borders (trees, ruins) are produced by evaluating the candidate
  features of all 3×3 neighbours and clipping them to the current chunk. No chunk ever reads
  another chunk's data.
- Stages: climate/biome → base terrain → surface → caves → ores → cross-chunk features.
- Version 2 added structures. It is implemented as "v1 plus extra placement gated on
  `version >= 2`", so v1 output is unchanged (golden test). The worldgen v2 test also checks
  that v2 differs from v1 only in structure chunks.
- Version 3 added villages. They are larger than the 3×3-chunk feature window, so each 8×8-chunk
  region decides deterministically whether it holds a village. Every chunk recomputes that
  region's layout, which is cheap, and keeps only its own blocks, so chunks still never read
  each other.
- Generated chests are empty. Loot is rolled from (seed, position) the first time a chest is
  opened, so the generated baseline never contains block entities.
- The generator version is stored in `world.json`. Old generators stay in the codebase. Changing
  output requires bumping `LATEST_GENERATOR_VERSION`, and the golden hash test enforces that.

## Sparse persistence

`Chunk::set()` is the only gameplay write path. It maintains `delta` (block index → the original
generated state):

- The first change records the original.
- Writing the original back erases the entry.
- So `delta.empty()` exactly means "blocks equal the baseline", regardless of the edit history.

A record stores only the current state at the delta positions, plus block entities, scheduled
ticks and entities. On load: regenerate, then apply. If no persistent data is left, the record is
deleted at the next save. Format details are in [docs/WORLD_FORMAT.md](docs/WORLD_FORMAT.md).

### Safe cleanup rules

- A record is deleted only by a successful journaled commit of an *empty* record, which happens
  only when the in-memory chunk is loaded and `hasPersistentData()` is false.
- Unloaded chunks are never inspected for deletion.
- `vxworld optimize` decodes each record against a regenerated baseline and re-encodes it. It
  removes a record only if the re-encoded result is empty, writes a backup first, and supports a
  dry run.
- Undecodable regions or records are left exactly as they are (`.corrupt` copy + log).

## Block updates and fluids

`game/behaviors.cpp` subscribes to World's neighbour-update, scheduled-tick and random-tick hooks.
Fluids are cellular: each cell re-derives its level from its feeders when it, or a neighbour,
changes. Nothing scans the world per frame.

- Scheduled ticks are persistent chunk data. They run in a total order (due tick, priority,
  position), so flows and future circuits behave identically on every platform and continue
  exactly where they stopped after a reload (`fluids_flow_resumes_identically_after_reload`).
- A flowing cell whose support weakens drains immediately. This avoids the slow
  "count-to-infinity" decay of mutually-feeding cells.

## Audio

`src/audio` (platform independent) has two parts:
- `SoundBank` synthesizes every sound from DSP recipes: filtered noise grains per material,
  damped resonators, and FM tones. The sounds are therefore original, need no disk space, and
  are deterministic per platform.
- `Mixer` renders interleaved stereo for sokol_audio's stream callback. Requests from the main
  thread go into a queue that the audio thread drains with `try_lock`, so the audio thread
  never blocks. Voices are stolen when all 32 are busy. Gains are computed once per block
  rather than per sample.

## Circuits

`game/circuits.cpp` defines which blocks emit power into which neighbours, and the rules for
strong and weak powering of solid blocks. The file header documents the full rule set.

- Wire strength is recomputed immediately on neighbour updates. This is bounded, because
  strength strictly decreases along a wire.
- Torches, lamps (turning off), relays, watchers, buttons, plates and pistons act through
  scheduled ticks.
- All updates go through a FIFO work queue rather than recursion.

## Containers and automation

`game/containers.cpp` holds the item-moving logic: kilns, hoppers and dispensing. It reaches
the world only through `World` and the `BlockBehaviors` hooks (`containerSignal`,
`onDispense`, `containerChanged`), so it runs headless in tests.
- Hoppers are processed in position order, so automation is deterministic.
- Any first access to a generated chest rolls its loot, whether the player or a hopper opens it.

## Memory management

- Uniform sections (all air, all stone) cost nothing. Mixed sections are 8 KiB plus light
  (4 KiB, shared when uniform).
- Copy-on-write `shared_ptr` sections let mesh and light jobs snapshot without locks or copies,
  unless a write actually happens.
- Budgets are in `MemoryBudget`. Current limit: the clean-chunk LRU (96 MiB default). Mesh and
  texture budgets come with the renderer.
- Measured resident cost: ~42 KiB per generated chunk (see PERFORMANCE.md).

## Threading

- **Main thread:** input, simulation tick (20 Hz fixed step), world update, GPU submission (sokol
  requires it).
- **Workers:** `hardware_concurrency - 1`. They run generation, lighting, record encoding for
  saves, and meshing (next).
- Main-thread result delivery is time-budgeted (`pumpMain(budgetMs)`) so a burst of finished
  chunks cannot cause a frame spike.

## Rendering

- Per 16³ section mesh with face culling against neighbours. Greedy merging of coplanar faces
  that have identical texture, tint and flat light/AO. Merged quads tile via REPEAT addressing.
- The vertex is 12 bytes packed (`render/mesher.h`): position in 1/16 units, face, AO, UV,
  texture layer, sky/block light, and RGB tint.
- One immutable GPU buffer per non-empty section holds the three layers back to back. Only dirty
  sections are re-meshed, so a block edit touches 1–3 sections. Edge neighbours are re-meshed
  too, because their smooth light samples across the border.
- Index buffer: one shared quad index buffer, no per-mesh indices.
- Three passes: opaque (front-to-back), cutout (alpha test), translucent (back-to-front by
  section).
- Frustum culling per section AABB. A cave-culling visibility graph per section (which faces
  connect through air) cuts underground overdraw.
- Reversed-Z infinite projection. A single 2D array texture stores 16×16 tiles with no bleeding
  and nearest sampling.
- Shaders are hand-written MSL and HLSL in `src/app/shaders.h`, with identical maths.
- UI: one transient vertex buffer per frame, one draw call.

## Platform abstraction

- sokol picks Metal or D3D11 at compile time.
- Paths go through `userDataDir()`: Application Support (macOS), `%APPDATA%` (Windows), XDG
  elsewhere.
- UTF-8 paths are handled through `pathFromUtf8()`.
- High-DPI comes from sokol's framebuffer vs window size.
