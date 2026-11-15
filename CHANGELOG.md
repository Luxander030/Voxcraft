# Changelog

## Unreleased

### Stair corners, fence gates and walls
- Stairs form inner and outer corners from their neighbours (a `shape` state property refreshed
  by neighbour updates, like fence connections). Stairs saved before this change load as straight
  stairs and update when a neighbour changes.
- Fence gates (oak, birch, pine): join fences along their axis and open with right-click or a
  signal. An open gate has no collision, and creatures path through it.
- Walls (cobblestone, mossy cobblestone, stone brick, slate, brick): thick posts, with the post
  dropped on straight runs. They join walls, gates and solid blocks and are 1.5 blocks tall for
  collision.
- Tests: `building_stair_corners_inner_and_outer`, `building_gates_and_walls`.

### Building shapes
- 9 slabs, 8 stairs, 3 fences, glass panes, oak and iron doors, and oak trapdoors, with recipes.
  Two slabs of the same kind stack into a double slab. Stairs and trapdoors take the upper half
  when placed against a ceiling or the upper half of a wall. Stairs rise away from the player.
  Fences and panes connect to each other and to solid walls (panes also connect to glass).
  A second door beside the first mirrors its hinge to form a double door.
- Doors and trapdoors open with right-click (both door halves together). Iron doors open only
  with a circuit signal, and any door or trapdoor follows a signal. Breaking either door half
  removes the other, and only one door item drops. A double slab drops two slabs.
- **Engine:** each state's visual and collision boxes are precomputed once at load, and
  connections are block-state properties refreshed by neighbour updates. Meshing, collision
  and saves therefore never look at neighbours. The mesher skips boundary faces against opaque
  neighbours and lights them from the cell they face.
- **Physics:** collision now uses exact box sweeps (full cubes plus shape boxes). The player steps
  up ledges of up to 0.6 blocks (slabs, stairs) without jumping. Fences are 1.5 blocks tall for
  collision and cannot be jumped.
- **Picking:** rays hit the actual boxes, so you can aim through an open doorway, and they return
  the hit point (used to choose slab and stair halves).
- Flat worlds no longer draw distant-terrain LOD, which sampled the default terrain height.
- Creature pathfinding walks through open doors and trapdoors, treats closed ones as walls, and
  never plans to stand on fences or panes.
- `--menu build` smoke scene. Tests: `building_*` (8).

### Third-person camera and player model
- V (remappable as "Camera view") cycles first person, behind the player and facing the player.
  The camera pulls in when a solid block is in the way. Grass, flowers and torches do not block it.
- The wanderer, an original player character (OpenSCAD `wanderer.scad`, 5 new 16×16 textures),
  is shown in third person. Animations: idle, walk/sprint (stride scales with speed), sneak lean,
  jump/fall, swim, climb, swing on attack/dig/use, arm raised while eating or drawing a bow, hurt
  flash, and death. The held item is shown in the right hand.
- `--view 0|1|2` starts in a given view (smoke testing).
- **Fix:** creature animation signs were inverted. Arm attacks swung backwards, grazers raised
  their heads to graze, puffbirds pecked upward, and head-look tilted the wrong way. A sign convention
  is now documented in `animations.json`, and `anim_directions_attack_forward_head_look_up` pins it.
- **Fix:** creature faces were drawn for a 16-pixel-wide face, but head boxes only sample their
  top-left w×h pixels. Settlers and gloomlings showed one off-centre eye, and most mouths were cut
  off. `face()` now takes the head size and places the eyes and mouth symmetrically inside it.

### Fixed: a failed final save no longer discards the session (data integrity)
- **Bug:** quitting to the title screen, closing the window or travelling through a rift closed the
  world even when its save failed (disk full, read-only folder). Every unsaved change was lost, and
  the only sign was a log line.
- **Fix:** the world stays open when the save fails. Travel is cancelled, quit-to-title stays in the
  game, closing the window is cancelled, and a status message shows the error. F5 also reports
  failures. Verified by running the game with a read-only save folder.

### Fixed: scheduled ticks fired early after chunk-cache reuse
- **Bug:** a chunk restored from the disk record resumed its pending ticks with their remaining
  delay, but a chunk reused from the clean-chunk cache fired all overdue ticks at once. Circuit and
  fluid timing depended on whether the chunk happened to be cached.
- **Fix:** cached chunks record their unload tick, and their ticks are shifted on reuse, matching
  the disk path.
- **Test:** `scheduled_ticks_frozen_while_unloaded_cache_or_disk` checks both paths. It fails
  without the fix.

### Fixed: stale autosave could overwrite a newer save (data integrity)
- **Bug:** a background autosave that reached the disk after a later synchronous save (for example,
  saving on quit) wrote its older chunk data and player state over the newer save.
- **Fix:** saves are sequence-numbered, and a commit older than the last committed save is skipped.
  Its records are still pending, so the newer save already included them.
- **Test:** `storage_stale_async_save_never_overwrites_newer_data` holds an autosave back until a
  newer save has finished. It fails without the fix.

### Fixed: entities lost through the chunk cache (data integrity)
- **Bug:** a chunk unloaded into the clean-chunk cache and then reused lost its dropped items and
  creatures, and the next save also removed them from the chunk's record.
- **Fix:** cached chunks now keep their entities and return them when reused.
- **Test:** `entities_survive_unload_into_cache_and_back` reproduces the bug.
- **Stress test:** `persistence_stress_random_edits_unloads_saves_reopens` runs several hundred
  random edits, chests, dropped items, unloads (cache on and off), saves and reopens, and checks
  everything against an independent model. It fails if this bug is reintroduced.

### Controls and accessibility
- Key remapping, invert mouse, toggle sneak and sprint, and sound subtitles.
- Settler and cinderwisp call sounds.

### Generator v4: biome variety
- Mossy boulders, lily pads (placed on water, pop off without it), capshrooms, ice spires and
  flower meadows.

### Lighting performance
- About 33% faster chunk lighting with identical output (pinned by a test).

### Distant terrain
- A level-of-detail horizon beyond the render distance (settings toggle) and a `--menu vista`
  smoke view.

### Smooth fluids
- Water and lava surfaces slope between levels (per-corner heights).

### Memory pressure
- An adaptive budget policy lowers the render distance and chunk cache under memory pressure,
  then recovers. The debug overlay shows the budget state. Physical memory is queried on every
  platform.

### Settlers and trading
- Settlers populate villages the first time a player comes near, and stay near home.
- A trade screen with deterministic offers that use copper ingots. Mob records (v2) store each
  settler's home and seed.

### Optimization
- Occlusion culling with a section connectivity graph (F6 toggles it). Underground scenes draw
  about 99% fewer triangles.
- LRU bound on the region-file cache.
- Smoke runs only bring the window to the front when a screenshot is requested.

### Farming, armor and bedrolls
- Hoes, farmland with moisture, grain growth stages, seeds and bread.
- Twelve armor pieces with inventory slots, damage reduction, durability and an armor bar.
- Bedrolls skip the night and set the spawn point.
- Growth-stage texture variants (`<name>_0` → `<name>_<age>`). The asset packer now rebuilds
  when its own rules change.

### Generator v3: villages
- Villages with a plaza, well, lamp posts, furnished cottages and paths. New worlds only; v1 and
  v2 worlds are unchanged.
- `/locate village`.

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
