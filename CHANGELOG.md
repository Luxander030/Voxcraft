# Changelog

## Unreleased

### Phase 8: circuits
- Glimmer circuits: levers, buttons, pressure plates, wire, inverting spark torches, lamps,
  relays with 4 delays, watchers and pistons.
- Updates go through an iterative FIFO queue. Timing is verified by tests, including a torch
  clock that resumes tick-for-tick after a save and reload.
- Lit texture variants for lamps and kilns (`_off` → `_on`).

### Phase 7: block updates
- Fluid flow (water and lava) on scheduled ticks: levels, falling columns, renewable water, lava
  hardening into slate or cobblestone. Deterministic, and resumes identically after a reload.
- Falling sand and gravel, unsupported plants breaking, turf spreading and decaying, saplings
  growing.
- Buckets.
- Scheduled ticks run in a platform-independent total order.

### Phase 5–6: survival gameplay
- Item registry with block items, materials, tiered tools, food and fuel, plus original item textures.
- Inventory with stacking and container click semantics, crafting (2×2, 3×3, tags, mirroring),
  kiln smelting, chests.
- Survival rules: break time by hardness and tool, harvest tiers, durability, health, hunger,
  regeneration, fall damage, drowning, lava, death, respawn.
- Dropped item entities with physics, merging, pickup and per-chunk persistence.
- Screens: inventory, workbench, chest, kiln, creative palette, death; HUD hearts, hunger and air.

### Phases 1–2: playable renderer
- Window, input and GPU through sokol (Metal / D3D11), with hand-written MSL and HLSL shaders.
- Original procedural block textures, plus an asset packer with validation, deduplication and
  incremental rebuilds.
- Section mesher: culling, greedy merging, smooth light, AO, plants, fluids and small shapes.
  It re-meshes only dirty sections, on worker threads.
- Opaque, cutout and translucent passes; reversed-Z; frustum culling; fog; day/night cycle.
- Player physics: walk, sprint, sneak edge guard, jump, fly, swim, climb, exact collision.
- Raycast targeting with break, place and pick. The player's state is saved with the world.
- Original bitmap font, HUD, hotbar with block icons, block outline, F3 debug overlay.
- `--smoke` scripted run and `--screenshot`, which captures only the game window.

### Engine core
- Engine core:
  - Types and math, logging, binary I/O, CRC32, LZ4, atomic and durable file writes, JSON.
  - Job system with dynamic priorities.
  - Deterministic noise and profiler counters.
- World:
  - Data-driven block registry with flattened states.
  - Chunks with uniform/copy-on-write sections and exact modification tracking.
  - Deterministic generator v1.
  - Snapshot-based lighting, plus streaming with hysteresis and an LRU cache.
- Storage:
  - Sparse chunk records in region files.
  - Journaled multi-file atomic saves with crash recovery.
  - Corrupt-region quarantine.
  - Full→sparse migration and baseline cleanup (`vxworld`).
- Tests: storage Tests A–F plus streaming, lighting, corruption and migration tests.
  `voxbench` benchmarks.
- Docs: the full set required by the project brief.
