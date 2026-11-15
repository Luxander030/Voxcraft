# Changelog

## Unreleased

### Sky and weather
- Sun, moon, stars and clouds.
- Rain and snow with persistent timers, overcast lighting, a rain sound and the `/weather`
  command.

### Ranged combat and the cinderwisp
- Projectiles (arrows, ember bolts), a bow with draw strength, and arrow recipes. Dispensers
  shoot arrows.
- Cinderwisp: a flying, ranged creature of the Hollow. Creature spawning is now restricted per
  dimension.
- Per-dimension ambient light in the terrain shader (Metal and HLSL).

### Circuits: automation
- Sticky pistons, comparators, hoppers, droppers and dispensers, plus recipes for every circuit
  block. New container screen layouts.
- Containers module (headless, tested). Kilns now tick through it.

### Dimensions
- The Hollow: a cavern dimension with a lava sea, new blocks (hollowrock, ember ore, ash soil,
  glowcaps) and items (ember shard, ember striker).
- Rift gates: light a closed slate frame with an ember striker. Travel uses 1:8 coordinates and
  arrival gates are found or built. Each dimension saves in its own folder.

### Generator v2: structures
- New worlds get watchtowers, buried caches and desert wells. Existing worlds keep generator v1
  unchanged.
- Structure loot is rolled lazily and deterministically when a chest is first opened, so
  structures add no storage.

### Phase 10: packaging and CI
- `vxshadercheck` compiles every HLSL shader with the D3D compiler in Windows CI.
- `tools/package.py` builds self-contained zips. CI validates assets, then packages and uploads
  the builds for all three platforms.

### Phase 9: particles
- Particle system: debris, puffs, flames and smoke, splashes. Particles are billboards in the
  entity pass.
- Eight-stage crack overlay. The block outline now follows the block's shape.
- The smoke-test script keeps control when focus changes during window activation, and
  `--menu pause` captures the pause menu.

### Phase 9: audio
- Procedural original sound bank and software mixer (positional, six categories, voice
  stealing), with generative ambient music and cave ambience.
- Sound hooks across gameplay and UI. Volume settings take effect immediately.

### Phase 9: menus
- Title screen and world list: play, create, and delete (moved to `saves/.trash`, so it can be
  recovered). World creation takes a name, seed, game mode and world type.
- Settings: render distance, FOV, mouse sensitivity, GUI scale and six volume categories, saved
  in `options.json`. Pause menu.
- Chat and commands.
- The app now switches between worlds: one-time GPU setup, per-world sessions, and a renderer
  epoch so stale mesh jobs from a closed world are dropped.
- Font descenders.

### Creatures
- OpenSCAD box-model pipeline, plus four original creature models and their skins.
- Animation system: bone hierarchy, keyframed clips, crossfades, state machine, procedural
  layers and distance-based LOD.
- AI: wandering, panicking, chasing, melee, deterministic A* pathfinding, spawning with caps,
  despawning, daylight burning. Creatures persist with their chunk.
- Melee combat, loot (venison, fowl, feathers, hide, shadow thread, flint), cooked foods.
- Entity render pass (CPU-skinned boxes, transient buffer, one draw call). Dropped items are now
  visible.
- Fix: flat worlds spawned the player at sea level, high above the ground.

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
