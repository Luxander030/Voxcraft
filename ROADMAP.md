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
- ✅ Cave/occlusion culling via section connectivity graph (−99% draws underground, −26% on hilly surfaces).
- ⬜ LOD for far terrain.

## Phase 3 — World and chunk foundation ✅
- ✅ Block registry, chunk data, modification tracking, deterministic generator v1, lighting, sparse journaled storage (Tests A–F), streaming.

## Phase 4 — Optimization foundation 🔶
- ✅ Background generation/lighting/meshing, chunk cache budget, storage cleanup, save validation, `vxworld`, `voxbench`, profiling overlay.
- ✅ Region-file cache bounded (LRU, lossless eviction).
- ✅ Memory budgets with pressure response: mesh VRAM (192 MiB) and process memory (half of physical RAM, max 4 GiB) are checked every second; over budget the effective render distance steps down (min 4) and the clean-chunk cache shrinks; it recovers with hysteresis. Unsaved edits are never dropped (only clean data is evicted).

## Phase 5 — Player gameplay 🔶
- ✅ Movement (walk, sprint, sneak with edge guard, jump, fly, swim, climb), exact swept AABB collision, raycast targeting, break/place/pick, player save.
- ✅ Survival breaking: hardness × tool type/tier, harvest rules, break progress, tool durability.
- ⬜ Third-person camera, crack overlay on the block being broken.

## Phase 6 — Core gameplay 🔶
- ✅ Data-driven item registry (`items.json`): block items, materials, 16 tiered tools, food, fuel. 31 original item textures.
- ✅ 36-slot inventory, stacking, click/right-click/shift-click semantics, drop (Q), creative palette.
- ✅ Crafting (`recipes.json`): shaped recipes with tags and mirroring, 2×2 inventory grid, 3×3 workbench, generated tool recipes.
- ✅ Kiln smelting block entity (fuel, progress, lit state); chests (27 slots, persisted).
- ✅ Health, hunger, saturation, exhaustion, regeneration, starvation, fall damage, drowning, lava, void; death drops items; respawn.
- ✅ Dropped item entities: physics, merging, pickup, despawn, persisted with their chunk.
- ✅ Game modes: survival / creative (F4 toggles; `--creative` for new worlds).
- ✅ Creatures (all original): grazer and puffbird (passive), gloomling (hostile, burns in daylight), rockmite (hostile, underground).
- ✅ OpenSCAD model pipeline (`assets/models/src/*.scad` → validated STL render + box-model JSON), hierarchical bones, keyframed clips with interpolation, crossfade blending, state machine, procedural walk/flap/head-look, animation LOD.
- ✅ AI: wander, panic, chase, melee; deterministic A* pathfinding with node budget; spawning by light/biome/depth with population caps; despawning; per-chunk persistence.
- ✅ Melee combat with weapon damage, knockback, invulnerability window, loot; creatures damage the player.
- ✅ Dropped items rendered as spinning blocks/cards.

## Survival depth ✅
- ✅ Farming: hoes (4 tiers) till soil, farmland moisture from nearby water, grain with 8 growth stages on random ticks (faster when moist), seeds from tall grass, bread.
- ✅ Armor: 12 pieces (hide/iron/azurite × head/chest/legs/feet), inventory armor slots, up to 80% attack damage reduction, durability, HUD armor bar, saved in player.dat v4.
- ✅ Bedrolls: sleep through the night when no hostile creature is near; sets the spawn point.

## Phase 7 — World generation & block updates 🔶
- ✅ Generator v1: biomes, terrain, caves, ores, trees (Phase 3).
- ✅ Event-driven fluids: water/lava flow with levels, falling columns, renewable water, lava hardening, drain-on-lost-support; deterministic and resumable across save/reload.
- ✅ Falling sand/gravel, plants losing support, turf spread/decay, saplings growing into trees.
- ✅ Buckets (scoop and pour sources).
- ✅ Generator v2 (new worlds): watchtowers, buried caches, desert wells. v1 kept bit-identical for existing worlds (golden test); unknown future versions are refused.
- ✅ Structure chests are generated empty and roll deterministic loot on first open, so structures never make untouched chunks persistent.
- ✅ Generator v3 (new worlds): villages — plaza with well and lamp posts, 4-7 furnished cottages facing it, gravel paths; region-based layout so multi-chunk structures stay chunk-independent; `/locate village`.
- ✅ Settlers: original villager creature (OpenSCAD model), spawned lazily the first time a player approaches a village (recorded in world.json so it happens once; generation stays entity-free), wander around their home plaza, trade 3 deterministic offers each using copper as currency.
- ✅ Smooth fluid surfaces: per-corner heights averaged over neighbouring fluid cells, sloped top and side faces.
- ⬜ More biome features.

## Phase 8 — Advanced gameplay 🔶
- ✅ Glimmer circuits: lever, button, pressure plate, wire (strength 15, −1/block, step up/down), inverting spark torch, lamp, relay (4 delays), watcher (change detector), piston (pushes 12).
- ✅ Strong/weak block powering, iterative FIFO update queue (no recursion), deterministic timing verified by tests including a torch clock that resumes identically after reload.
- ✅ Lit texture variants (lamp, kiln).
- ✅ Second dimension, the Hollow: sealed lava-sea caverns (hollowrock, ember ore, ash soil, glowcaps, hanging glowcrystal), generator from v2, 4-block-grid interpolated density.
- ✅ Rift gates: slate frame lit with a crafted ember striker (flood fill in either plane, max 21 cells), collapse when the frame breaks, 1:8 travel with find-or-build arrival gates, per-dimension save folders, player file records the dimension.
- ✅ Sticky pistons (pull back), comparators (container fullness or signal compare/subtract, drives wire at its strength), hoppers (push/pull every 8 ticks, vacuum dropped items, feed kilns by face, locked while powered), droppers and dispensers (rising-edge, pour/scoop fluids, insert into containers), recipes for all circuit blocks.
- ✅ Ranged combat: projectile system (swept segments vs blocks, creatures, player; arrows stick and can be picked up), bow with draw strength, arrows, dispensers fire arrows.
- ✅ Cinderwisp: flying ranged Hollow creature (keeps range, strafes, shoots ember bolts only with line of sight); creatures restricted per dimension; "air" spawn rule; emissive rendering.
- ✅ Per-dimension ambient light (the Hollow is dim but never pitch black).

## Phase 9 — Presentation 🔶
- ✅ Title screen, world list (play/create/delete-to-trash), world creation (name, seed, mode, type), settings (render distance, FOV, sensitivity, GUI scale, 6 volume categories) persisted in `options.json`, pause menu with save & quit to title.
- ✅ Chat with commands: /help /gamemode /time /tp /give /seed /spawnpoint /kill /summon.
- ✅ Font descenders.
- ✅ Audio: 51 original sounds synthesized at startup (1.6 MiB, 18 ms; no audio files), own mixer with 32 voices, positional attenuation + equal-power panning, 6 volume categories, generative ambient music, cave ambience; footsteps/dig/break/place per material, creature calls, UI, pickup, hurt, eat, splash, circuit clicks.
- ✅ Particles (pooled, fixed capacity): block debris on dig/break, creature death puffs, torch flames + smoke, water splashes; crack overlay showing break progress; outline fits the block's shape.
- ✅ Sky: sun and moon following the day cycle, stars at night, drifting block clouds (unlit/unfogged sky objects occluded by terrain via depth).
- ✅ Weather: persistent rain/snow spells (world.json), precipitation only over sky-exposed ground, overcast lighting and fog, rain ambience, /weather.

## Phase 10 — Cross-platform stabilization 🔶
- ✅ CI on macOS arm64, macOS x64 and Windows x64: asset validation (model staleness, texture sizes, references), build, tests incl. cross-platform golden, HLSL shader compilation (`vxshadercheck`), benchmark, packaging, artifact upload.
- ✅ `tools/package.py`: self-contained zip (macOS `.app` with resources, ad-hoc signed; Windows exe + data). ~0.5 MiB.
- ⬜ Run the Windows build on real hardware; notarized macOS builds; installer.

## Phase 11 ⬜
Long-term expansion (CLAUDE.md §25): structures, dimensions, more content, further optimization.

## Known limitations
- Windows: compiles in CI, but the D3D11 path (HLSL shaders compile at runtime) has not been run on a Windows machine.
- Generator golden hash pinned on arm64; CI checks x64.
