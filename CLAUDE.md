# PROJECT: INDEPENDENT MINECRAFT JAVA EDITION RECREATION
## Cross-Platform Windows + macOS | Performance-First Architecture

You are tasked with developing a substantial, independently implemented recreation of Minecraft Java Edition.

The most recently installed Minecraft Java Edition version on this computer will serve as the primary reference for gameplay, mechanics, functionality, world generation, and visual conventions.

The game must support:

- Microsoft Windows.
- Apple macOS.
- Apple Silicon.
- Intel-based Macs where technically supported.
- Modern Windows x64 systems.

Both platforms must be supported from the beginning of development.

This is a long-term game development project. Treat it as the development of an independent game engine and a complete game, not as a small voxel sandbox experiment.

## FUNDAMENTAL PROJECT REQUIREMENTS

The project must have:

1. An independently developed game engine.
2. An independently developed rendering system.
3. Original block, item, and entity models.
4. Original textures.
5. Original animations.
6. Original audio assets.
7. Independently implemented gameplay mechanics.
8. An independently implemented world-generation system.
9. An independent world-save format.
10. A shared Windows and macOS codebase.
11. An automated asset-development pipeline.
12. An aggressive but safe optimization strategy.
13. Comprehensive documentation.
14. Automated testing and cross-platform builds.

**Performance and resource efficiency are first-class design requirements, not optional polish.**

Whenever implementing a system, consider whether its implementation can be made more efficient in terms of:

- CPU usage.
- GPU usage.
- RAM consumption.
- VRAM consumption.
- Disk storage.
- Disk I/O.
- Network bandwidth, if multiplayer is implemented.
- Startup time.
- Loading time.
- Background processing.
- Battery consumption.
- Overall responsiveness.

Do not sacrifice correctness, data integrity, or gameplay functionality for superficial performance improvements.

All optimization must be measurable, safe, and maintainable.

---

# 1. INITIAL MINECRAFT INSTALLATION DISCOVERY

Before implementing the game, inspect the existing Minecraft Java Edition installation.

The current development environment is macOS.

The normal installation directory is:

`~/Library/Application Support/minecraft`

Do not blindly assume this is the correct location.

Investigate launcher configurations and available installation directories.

## 1.1 Identify the Most Recently Installed Version

Determine:

- All installed Minecraft Java Edition versions.
- Version identifiers.
- Launcher profiles.
- Installation timestamps where available.
- Relevant metadata.
- Configuration files.
- Available resource-pack configurations.
- Relevant gameplay settings.
- World-generation settings.

Select the most recently installed version, not simply the highest numerical version.

If the information is ambiguous, present the available candidates and ask me which one to use.

Record the selected version in `REFERENCE_NOTES.md`.

## 1.2 Investigate the Reference

Use the installation as a behavioral and technical reference.

Investigate:

- Block properties.
- Item behavior.
- Entity behavior.
- Player movement.
- Physics.
- Collision.
- Inventory.
- Crafting.
- World generation.
- Biomes.
- Dimensions.
- Redstone.
- Fluid mechanics.
- Lighting.
- Animation.
- UI.
- Audio behavior.
- Chunk management.
- Saving and loading.
- Other important game systems.

Document observations and implementation requirements.

Do not attempt to implement every system before establishing the basic architecture.

## 1.3 Reference Restrictions

The original Minecraft installation is for reference only.

Do not:

- Extract original Minecraft textures for project use.
- Extract original models.
- Reuse original animations.
- Reuse original audio.
- Copy proprietary source code.
- Bundle the original Minecraft JAR.
- Use the original game as a runtime dependency.
- Redistribute original Minecraft libraries.
- Modify the original installation.

Implement all systems independently and create original assets.

The final game must run without Minecraft installed.

---

# 2. CROSS-PLATFORM TECHNOLOGY

Choose a technology stack that supports Windows and macOS from the beginning.

Evaluate:

- Rust.
- C++.
- wgpu.
- SDL3 GPU.
- Metal.
- Direct3D 12.
- Suitable cross-platform rendering abstractions.
- Appropriate windowing and input libraries.
- Audio libraries.
- Physics libraries.
- Serialization libraries.
- Asset-loading libraries.

Rust + wgpu is an initial candidate worth investigating, but it is not a mandatory decision.

C++ is also a viable candidate.

Compare technologies based on:

- Performance.
- Memory management.
- Portability.
- Graphics support.
- Multithreading.
- Development complexity.
- Ecosystem maturity.
- Long-term maintainability.
- Licensing.

Present your recommendation before committing.

## 2.1 Shared Codebase

Use one shared game codebase.

Keep platform-specific code isolated behind interfaces.

The following should behave consistently across Windows and macOS:

- Gameplay.
- Rendering conventions.
- World generation.
- Block definitions.
- Item definitions.
- Entity behavior.
- Physics.
- Animations.
- Save formats.
- Configuration.
- Asset formats.

Avoid maintaining separate implementations of the game.

## 2.2 Platform-Specific Considerations

Account for:

- Windows filesystem conventions.
- macOS filesystem conventions.
- Different graphics backends.
- Input differences.
- Audio devices.
- Application-data locations.
- High-DPI displays.
- Different CPU architectures.
- Different GPU capabilities.
- Memory availability.

Use platform-aware APIs and path libraries.

Do not hardcode user directories.

---

# 3. PERFORMANCE-FIRST ENGINEERING PHILOSOPHY

This section is mandatory and applies to every subsystem.

Do not wait until the project becomes slow before considering optimization.

Every major system should be designed with resource efficiency in mind.

## 3.1 Optimization Priorities

Prioritize the following:

1. Avoid unnecessary work.
2. Avoid unnecessary data storage.
3. Avoid unnecessary memory allocation.
4. Avoid unnecessary disk operations.
5. Avoid unnecessary GPU operations.
6. Avoid unnecessary CPU calculations.
7. Avoid unnecessary object creation.
8. Avoid duplicate data.
9. Avoid blocking the main thread.
10. Reuse resources wherever practical.

Whenever possible, prefer eliminating unnecessary work over making unnecessary work execute slightly faster.

For example, if a chunk has never been modified and can be regenerated deterministically, do not save its complete block data merely because it was loaded.

## 3.2 Measure Before and After

Use profiling and benchmarking.

For significant optimization changes, document:

- Previous behavior.
- New behavior.
- Performance measurements.
- Memory usage.
- Storage usage.
- CPU impact.
- GPU impact.
- Any trade-offs.

Do not claim an optimization works without verification.

Do not introduce complicated optimizations that provide no meaningful measurable benefit.

Avoid premature micro-optimization.

However, do establish efficient architectural foundations from the beginning.

## 3.3 Correctness Comes First

Optimization must never silently change gameplay behavior.

Do not remove information that could affect:

- Player-created structures.
- Block states.
- Containers.
- Entities.
- Redstone.
- Scheduled block updates.
- Fluids.
- World time.
- World generation.
- Other persistent gameplay systems.

Every optimization must preserve the game's intended behavior.

---

# 4. WORLD STORAGE AND CHUNK OPTIMIZATION

This is one of the highest-priority optimization systems.

Design the world-storage architecture so that it stores only the information that actually needs to be preserved.

## 4.1 Deterministic World Generation

Every world must have a persistent generation configuration containing:

- World seed.
- Generator version.
- Generator settings.
- Biome configuration.
- Dimension configuration.
- Relevant generation parameters.

World generation should be deterministic.

Given identical:

- Seed.
- Generator version.
- Generator settings.
- Dimension.
- Chunk coordinates.

The game should be able to reproduce the same original generated chunk.

The generation version must be preserved so that future game updates do not accidentally generate different terrain for existing worlds.

## 4.2 Do Not Persist Unmodified Generated Chunks

Implement a system that distinguishes between:

- Newly generated chunks.
- Loaded but unmodified chunks.
- Modified chunks.
- Chunks that have returned to their original generated state.
- Chunks containing persistent non-terrain data.

**A chunk that has never been modified should not need to occupy permanent world-save storage.**

If a chunk can be regenerated exactly from its world-generation information, the game should be able to recreate it whenever needed.

Do not serialize and permanently store complete block data for every generated chunk by default.

Instead:

1. Generate the chunk when required.
2. Keep it in memory while needed.
3. Track modifications.
4. Determine whether persistent data exists.
5. Save only necessary changes.
6. Regenerate unmodified terrain when it is needed again.

This should substantially reduce world-save sizes.

## 4.3 Modified Chunk Detection

Implement reliable modification tracking.

Track changes to:

- Blocks.
- Block states.
- Containers.
- Block entities.
- Fluids.
- Redstone-related persistent state.
- Entities.
- Scheduled updates.
- Other persistent chunk data.

A chunk should be considered modified if it contains persistent information that differs from its reproducible generated baseline.

Do not rely solely on whether a block has been interacted with.

A player might place a block and later remove it, restoring the original terrain.

In that case, if the chunk has no other persistent modifications, it should be eligible for removal from persistent storage.

Compare the final persistent state against the generated baseline when necessary.

## 4.4 Sparse Chunk Serialization

Instead of saving entire chunks, investigate sparse serialization.

Store only information that differs from the original generated terrain.

For example:

- Changed block positions.
- Replacement block identifiers.
- Removed generated blocks.
- Modified block states.
- Persistent block entities.
- Persistent entity data.
- Relevant scheduled updates.

Use compact representations.

Avoid storing large amounts of redundant information.

## 4.5 Automatic Storage Cleanup

Implement automatic cleanup of redundant chunk data.

The cleanup system should identify:

- Chunks that contain no persistent modifications.
- Chunks whose changes have been completely reverted.
- Empty modification records.
- Redundant serialized data.
- Obsolete temporary files.
- Unreferenced generated cache data.

When a chunk is confirmed to have returned to its original generated state and has no persistent information, its saved modification record can be removed.

The chunk can then be regenerated.

However:

- Never delete a modified chunk simply because it is currently unloaded.
- Never delete player-created structures.
- Never delete container contents.
- Never delete persistent entities.
- Never delete scheduled updates that affect gameplay.
- Never delete data whose regeneration is not deterministic.
- Never delete data during an incomplete or failed save operation.

Cleanup must operate on validated persistent state.

## 4.6 Existing Chunk Cleanup

If older versions of the project save complete chunks, implement a migration or cleanup system.

It should:

1. Read the existing chunk data.
2. Reconstruct its generated baseline.
3. Compare saved data against the baseline.
4. Extract persistent modifications.
5. Verify all persistent data has been preserved.
6. Save the optimized representation.
7. Validate the new representation.
8. Only then allow obsolete storage to be reclaimed.

Do not perform destructive migrations without backups.

## 4.7 World Storage Format

Use a compact, versioned, cross-platform format.

Investigate:

- Binary serialization.
- Region-based storage.
- Sparse block changes.
- Compression.
- Checksums.
- Journaling.
- Atomic writes.
- Incremental persistence.

Avoid saving thousands of tiny files if a more efficient region-based format is appropriate.

The world format must be documented.

Worlds must remain compatible between Windows and macOS.

---

# 5. MEMORY MANAGEMENT AND CHUNK LIFECYCLE

Memory consumption must be actively managed.

Do not keep every visited chunk permanently in RAM.

## 5.1 Chunk Lifecycle

Implement a well-defined lifecycle:

1. Unloaded.
2. Requested.
3. Generating.
4. Generated.
5. Meshing.
6. Ready.
7. Loaded.
8. Dirty.
9. Saving.
10. Cached.
11. Unloading.

Allow appropriate transitions.

Avoid duplicate generation requests.

Avoid generating the same chunk multiple times simultaneously.

## 5.2 Chunk Loading and Unloading

Implement distance-based chunk management.

Support:

- Render distance.
- Simulation distance.
- Loading distance.
- Unloading distance.
- Background loading.
- Background unloading.

Use hysteresis where appropriate to prevent chunks repeatedly loading and unloading near a boundary.

Prioritize nearby chunks.

Do not perform expensive chunk generation on the main game thread.

## 5.3 Memory Budgets

Introduce configurable memory budgets.

Track:

- Chunk data.
- Chunk meshes.
- Entity data.
- Texture memory.
- Model memory.
- Audio memory.
- Temporary generation buffers.
- GPU resources.

Implement a memory-aware cache.

When memory pressure increases:

1. Identify the least useful cached resources.
2. Release resources that can safely be reconstructed.
3. Unload distant inactive chunks.
4. Preserve persistent modifications.
5. Avoid unnecessary allocations.
6. Maintain responsive gameplay.

Never discard unsaved player modifications.

## 5.4 Object and Buffer Reuse

Investigate:

- Object pooling.
- Buffer reuse.
- Memory arenas.
- Reusable temporary arrays.
- Efficient data structures.
- Reduced heap allocation.

Use these techniques where they actually improve performance.

Avoid excessive allocation and deallocation in frequently executed code.

---

# 6. CPU AND MULTITHREADING OPTIMIZATION

Use multithreading appropriately.

Potential background tasks include:

- Chunk generation.
- Chunk meshing.
- World loading.
- World saving.
- Asset loading.
- Texture processing.
- Model conversion.
- Lighting calculations.
- Background validation.

## 6.1 Job System

Consider implementing a reusable job system.

Support:

- Worker threads.
- Task priorities.
- Task dependencies.
- Cancellation.
- Result delivery.
- Safe synchronization.
- Work scheduling.

Prioritize tasks based on relevance to the player.

For example, generating the chunk immediately in front of the player should generally take priority over generating a distant chunk.

## 6.2 Main Thread

Keep the main thread responsive.

Avoid blocking it with:

- Disk reads.
- Disk writes.
- Chunk generation.
- Mesh generation.
- Asset conversion.
- Large decompression operations.

Use appropriate asynchronous operations.

Ensure all graphics API operations occur on threads permitted by the selected rendering framework.

## 6.3 Data Structures

Choose data structures based on access patterns.

Avoid unnecessarily expensive:

- Hash lookups.
- Memory allocations.
- Repeated conversions.
- Deep object hierarchies.
- Redundant copies.

Investigate data-oriented design where appropriate.

Do not introduce complexity without a measurable reason.

---

# 7. GPU AND RENDERING OPTIMIZATION

The renderer must be designed for large voxel worlds.

## 7.1 Geometry

Implement:

- Greedy meshing.
- Face culling.
- Chunk mesh batching.
- Efficient vertex formats.
- Index buffers.
- Mesh reuse.
- Frustum culling.
- Appropriate occlusion optimizations.

Do not render invisible block faces.

Avoid unnecessary draw calls.

Avoid rebuilding entire chunk meshes when only a small area changes.

## 7.2 Level of Detail

Investigate appropriate level-of-detail strategies.

Consider:

- Distant terrain simplification.
- Reduced detail for distant entities.
- Distance-based animation updates.
- Simplified distant effects.

Preserve the intended visual style.

Do not introduce visual changes that make the game feel inconsistent.

## 7.3 GPU Resource Management

Implement:

- Texture atlases.
- Shared materials.
- Efficient GPU buffers.
- Resource caching.
- GPU memory monitoring where supported.
- Resource disposal.
- Minimized CPU-to-GPU transfers.

Avoid repeatedly uploading unchanged geometry or textures.

## 7.4 Lighting

Optimize lighting calculations.

Investigate:

- Incremental light updates.
- Chunk-level light data.
- Efficient propagation.
- Cached lighting results.
- Recalculation only where necessary.

Avoid recalculating entire worlds when a local change occurs.

## 7.5 Rendering Distance

Make render distance configurable.

Ensure performance degrades gracefully on weaker hardware.

Allow graphics settings appropriate for both integrated and dedicated GPUs.

---

# 8. DISK STORAGE AND ASSET OPTIMIZATION

The game should minimize unnecessary disk consumption.

## 8.1 Asset Deduplication

Avoid storing duplicate assets.

If multiple blocks or items use the same texture, reference the same texture asset.

Do not create identical copies unnecessarily.

Use shared models and materials when appropriate.

## 8.2 Compression

Investigate suitable compression methods for:

- World data.
- Texture assets.
- Model assets.
- Audio assets.
- Packed resources.

Consider compression ratio, decompression speed, memory use, and licensing.

Do not compress assets repeatedly during gameplay if they can be efficiently loaded and cached.

## 8.3 Asset Packaging

Use an organized asset packaging system.

Support:

- Asset manifests.
- Asset identifiers.
- Asset versions.
- Dependency tracking.
- Validation.
- Compression.
- Incremental rebuilding.

Only rebuild assets that have changed.

## 8.4 Temporary Data

Manage temporary data intelligently.

Remove obsolete temporary files when safe.

Keep temporary data separate from permanent world saves.

Avoid leaving abandoned cache files indefinitely.

## 8.5 Cache Management

Implement cache policies for:

- Textures.
- Models.
- Chunk meshes.
- Generated chunks.
- Audio.
- Converted assets.

Support cache invalidation when source assets or generation versions change.

Cached data must never be treated as the only copy of player-created information.

---

# 9. TEXTURE CREATION

All textures must be independently created.

Preserve the low-resolution pixel-art aesthetic.

## 9.1 Texture Resolution

Standard blocks and items must use 16×16 textures by default.

Examples:

- Stone: 16×16.
- Dirt: 16×16.
- Grass faces: 16×16.
- Wood: 16×16.
- Ores: 16×16.
- Standard item icons: 16×16.

Specialized assets may use other resolutions when justified.

Entity texture atlases, GUI assets, and animated effects may require different dimensions.

Document exceptions.

## 9.2 Texture Standards

Use:

- PNG.
- Nearest-neighbor sampling.
- Crisp pixel boundaries.
- Appropriate transparency.
- Consistent palettes.
- Consistent art direction.

Avoid unwanted anti-aliasing and texture smoothing.

## 9.3 Texture Pipeline

Create:

- Texture import.
- Texture validation.
- Atlas generation.
- UV generation.
- Animation metadata.
- Missing-texture detection.
- Preview tools.

Implement automated dimension checks.

Standard 16×16 textures must be validated.

---

# 10. OPENSCAD MODELING

OpenSCAD is already installed.

Use the existing installation.

Do not reinstall it unnecessarily.

## 10.1 Modeling

Create original models using OpenSCAD where appropriate.

Keep editable `.scad` sources.

Use reusable parametric modules.

Maintain consistent:

- Coordinates.
- Scale.
- Origins.
- Pivot points.
- UV mapping.
- Collision geometry.

## 10.2 Automated Export

Create scripts that:

1. Locate OpenSCAD.
2. Validate its availability.
3. Process source files.
4. Export geometry.
5. Check export results.
6. Convert geometry into runtime formats.
7. Validate model output.

Support Windows and macOS executable discovery.

## 10.3 Model Formats

Evaluate:

- OBJ.
- STL.
- 3MF.
- glTF where suitable.

Use efficient runtime formats.

Do not use complex meshes for ordinary cuboid blocks unnecessarily.

---

# 11. ANIMATION SYSTEM

Implement an independent animation system.

Support:

- Hierarchical bones.
- Keyframes.
- Interpolation.
- Blending.
- State machines.
- Procedural animation.
- Animation controllers.

Implement player animations:

- Idle.
- Walking.
- Running.
- Sneaking.
- Jumping.
- Falling.
- Swimming.
- Climbing.
- Attacking.
- Using items.
- Taking damage.
- Death.

Implement entity animations:

- Idle.
- Walking.
- Flying.
- Swimming.
- Attacking.
- Interacting.
- Taking damage.
- Death.

Optimize animation updates based on visibility and distance.

Distant entities may use reduced animation update frequencies where this does not create noticeable visual problems.

---

# 12. BLOCK, ITEM, AND ENTITY SYSTEMS

Create data-driven registries.

Blocks should support:

- Identifiers.
- Names.
- Textures.
- Models.
- Hardness.
- Resistance.
- Collision.
- Transparency.
- Light emission.
- Drops.
- Tool requirements.
- Block states.
- Interactions.

Items should support:

- Identifiers.
- Names.
- Textures.
- Stack limits.
- Durability.
- Equipment.
- Tool properties.
- Interactions.
- Consumable behavior.

Entities should support:

- Models.
- Animations.
- AI.
- Navigation.
- Collision.
- Health.
- Damage.
- Drops.
- Environmental interactions.

Avoid monolithic hardcoded registries.

Reuse common definitions.

Avoid duplicating identical assets or behavior.

---

# 13. GAMEPLAY SYSTEMS

Eventually implement:

## Player
- First-person movement.
- Third-person movement.
- Sprinting.
- Sneaking.
- Jumping.
- Swimming.
- Climbing.
- Gravity.
- Collision.
- Health.
- Hunger.
- Damage.
- Death.
- Respawning.

## World
- Chunk-based terrain.
- Block states.
- Biomes.
- Caves.
- Ores.
- Structures.
- Vegetation.
- Dimensions.
- Fluids.

## Inventory
- Hotbar.
- Main inventory.
- Item stacking.
- Item movement.
- Equipment.
- Containers.
- Crafting.
- Recipes.

## Entities
- Passive AI.
- Hostile AI.
- Neutral AI.
- Navigation.
- Pathfinding.
- Combat.
- Entity interactions.

## Technical Gameplay
- Redstone.
- Block updates.
- Neighbor updates.
- Scheduled ticks.
- Pistons.
- Hoppers.
- Comparators.
- Observers.
- Other technical mechanisms.

## Presentation
- Main menu.
- HUD.
- Inventory.
- Chat.
- Settings.
- Debug information.
- Lighting.
- Particles.
- Audio.

Implement these systems independently.

Do not substitute visual imitations for functional mechanics.

---

# 14. WORLD GENERATION

Implement deterministic procedural generation.

Support:

- Seed-based generation.
- Biomes.
- Terrain.
- Mountains.
- Oceans.
- Rivers.
- Caves.
- Ores.
- Structures.
- Vegetation.
- Dimensions.

Separate generation into appropriate stages.

Optimize generation for:

- CPU time.
- Memory.
- Parallel execution.
- Chunk streaming.
- Storage efficiency.

Avoid regenerating chunks unnecessarily.

Cache generation results only when doing so is more efficient than regenerating them.

Ensure cache invalidation is tied to generation versions and settings.

---

# 15. PHYSICS AND FLUIDS

Implement independent physics and collision systems.

Support:

- Gravity.
- Movement.
- Collision.
- Falling blocks where applicable.
- Entity collision.
- Projectile behavior.
- Fluid interactions.

Fluid systems should support:

- Water.
- Lava.
- Flowing behavior.
- Fluid levels.
- Fluid updates.
- Waterlogging.
- Rendering.

Optimize update scheduling.

Do not calculate every fluid block every frame if updates can be scheduled only when necessary.

---

# 16. REDSTONE AND BLOCK UPDATES

Implement functional technical gameplay.

Support:

- Signal propagation.
- Signal strength.
- Redstone dust.
- Torches.
- Repeaters.
- Comparators.
- Pistons.
- Observers.
- Hoppers.
- Dispensers.
- Droppers.
- Buttons.
- Levers.
- Pressure plates.
- Redstone lamps.

Use event-driven and scheduled update mechanisms where appropriate.

Avoid repeatedly recalculating unchanged circuits.

Ensure optimization does not change signal timing or behavior.

Use deterministic tests for complicated circuits.

---

# 17. AUDIO SYSTEM

Create an independent audio system.

Support:

- Environmental audio.
- Music.
- Footsteps.
- Block sounds.
- Entity sounds.
- UI sounds.
- Positional audio.
- Audio categories.
- Volume settings.

Use original or properly licensed audio.

Optimize audio resource loading and memory use.

Cache frequently used sounds appropriately.

---

# 18. USER INTERFACE

Create original interface artwork.

Support:

- Main menu.
- World selection.
- World creation.
- Settings.
- Pause menu.
- HUD.
- Inventory.
- Crafting.
- Chat.
- Debug screen.
- Accessibility settings.

Maintain the pixel-art aesthetic.

Support multiple resolutions, aspect ratios, and high-DPI displays.

Avoid unnecessary GUI redraws where practical.

---

# 19. MULTIPLAYER-READY ARCHITECTURE

Prioritize single-player first.

However, structure the game so multiplayer can eventually be introduced.

Consider:

- Authoritative server design.
- Entity synchronization.
- Block updates.
- Player synchronization.
- Inventory synchronization.
- Packet serialization.
- Network security.
- Latency handling.

Do not claim multiplayer support until implemented and tested.

---

# 20. CROSS-PLATFORM BUILDS

Support both Windows and macOS.

Use a suitable shared build system.

Implement:

### Windows
- Debug builds.
- Release builds.
- Executable packaging.
- Asset packaging.
- Runtime dependency handling.

### macOS
- Debug builds.
- Release builds.
- Apple Silicon builds.
- Intel builds where supported.
- `.app` packaging.
- Asset packaging.

## 20.1 Continuous Integration

Use GitHub Actions or a suitable alternative.

Implement separate Windows and macOS build jobs.

Each job should:

1. Check out the repository.
2. Install dependencies.
3. Compile the game.
4. Run tests.
5. Validate assets.
6. Package the game.
7. Report failures.

Do not assume one platform's successful build proves compatibility with the other.

## 20.2 Save Compatibility

Windows and macOS must share the same world-save format.

Worlds must be transferable between the platforms.

Avoid storing platform-specific data in world files unless absolutely necessary.

---

# 21. PROFILING AND OPTIMIZATION TOOLS

Build or integrate development tools that allow performance problems to be identified.

The development environment should provide visibility into:

- FPS.
- Frame time.
- CPU usage.
- GPU time.
- RAM consumption.
- VRAM consumption where available.
- Loaded chunks.
- Generated chunks.
- Modified chunks.
- Cached chunks.
- Chunk generation time.
- Chunk meshing time.
- Save time.
- Load time.
- Disk usage.
- Entity count.
- Draw calls.
- Triangle count.
- Memory allocation rates.
- Background job queues.

Create a developer debug overlay.

Allow performance statistics to be enabled or disabled.

Implement profiling hooks.

Avoid imposing substantial profiling overhead in normal release builds.

---

# 22. AUTOMATED OPTIMIZATION AND MAINTENANCE

Implement maintenance tools where appropriate.

Examples:

- Redundant chunk-data detection.
- Unmodified chunk storage cleanup.
- Empty modification-record removal.
- Asset deduplication.
- Cache cleanup.
- Orphaned asset detection.
- Texture validation.
- Model validation.
- Save-format verification.
- Unused asset detection.
- Dependency auditing.
- Storage usage reports.

Maintenance operations must be safe.

Provide dry-run functionality for destructive cleanup.

Show what will be removed before executing a potentially destructive operation.

Where appropriate, provide automatic backups.

Never silently delete player data.

---

# 23. PROJECT DOCUMENTATION

Maintain the following files throughout development.

## README.md
- Description.
- Features.
- Supported operating systems.
- Requirements.
- Build instructions.
- Installation.
- Runtime instructions.

## ROADMAP.md
- Development phases.
- Completed features.
- Current work.
- Upcoming work.
- Known limitations.
- Performance goals.
- Optimization tasks.

## ARCHITECTURE.md
- Module architecture.
- Rendering.
- Platform abstraction.
- Chunk management.
- Storage.
- Memory management.
- Threading.
- Asset systems.
- Optimization strategies.

## REFERENCE_NOTES.md
- Selected reference version.
- Gameplay observations.
- Block behavior.
- Item behavior.
- Entity behavior.
- World generation.
- Rendering behavior.

## ASSET_PIPELINE.md
- Texture creation.
- 16×16 texture requirements.
- OpenSCAD.
- Model conversion.
- UV mapping.
- Animation.
- Asset validation.

## PERFORMANCE.md
Document:

- Performance targets.
- Benchmark methodology.
- Profiling results.
- Memory budgets.
- CPU optimizations.
- GPU optimizations.
- Storage optimizations.
- Chunk optimization.
- Known bottlenecks.

## CHANGELOG.md
Record significant changes.

## CONTRIBUTING.md
Document coding standards, build processes, tests, and asset conventions.

Update documentation whenever relevant systems change.

---

# 24. TESTING AND DATA INTEGRITY

Every major system must have appropriate automated tests.

Test:

- Chunk generation.
- Chunk regeneration.
- Chunk modification tracking.
- Chunk serialization.
- Chunk deserialization.
- Chunk cleanup.
- Reverted block changes.
- Containers.
- Block entities.
- Scheduled updates.
- World saving.
- World loading.
- World migration.
- Save corruption recovery.
- Texture dimensions.
- Model conversion.
- Inventory behavior.
- Crafting.
- Physics.
- Collision.
- Redstone.
- Cross-platform compatibility.

## 24.1 Critical Storage Tests

Specifically verify:

**Test A: Unmodified chunk**

1. Generate a chunk.
2. Load it.
3. Make no modifications.
4. Save the world.
5. Verify that the chunk does not require persistent block-data storage.
6. Reload the world.
7. Verify that the chunk regenerates identically.

**Test B: Modified chunk**

1. Generate a chunk.
2. Modify a block.
3. Save.
4. Reload.
5. Verify that the modification remains.

**Test C: Reverted chunk**

1. Generate a chunk.
2. Modify a block.
3. Restore the original generated block.
4. Verify that no other persistent modifications remain.
5. Save.
6. Verify that the modification record can be removed.

**Test D: Persistent block entity**

1. Generate a chunk.
2. Place a container.
3. Store items inside it.
4. Save.
5. Reload.
6. Verify that the container and its contents remain intact.

**Test E: Cross-platform world**

1. Create a world on Windows.
2. Modify several chunks.
3. Save.
4. Open the same world on macOS.
5. Verify that all persistent data is intact.
6. Save again.
7. Reopen on Windows.

**Test F: Crash recovery**

1. Begin saving a modified world.
2. Simulate an interrupted save operation.
3. Restart the game.
4. Verify that the previous valid save or recovered state is available.
5. Verify that no player-created data is silently lost.

Do not mark storage optimization complete until these tests pass.

---

# 25. DEVELOPMENT MILESTONES

Develop incrementally.

Each milestone must result in a working and testable build.

## Phase 0 — Discovery
- Locate Minecraft.
- Identify the latest installed version.
- Inspect the development environment.
- Investigate OpenSCAD.
- Evaluate technology.
- Establish architecture.
- Create documentation.
- Establish optimization goals.

## Phase 1 — Cross-Platform Foundation
- Initialize the project.
- Establish the build system.
- Implement platform abstractions.
- Initialize window management.
- Initialize graphics.
- Implement input.
- Implement logging.
- Produce initial platform builds.

## Phase 2 — Rendering Foundation
- Camera.
- Geometry.
- Shaders.
- Texture loading.
- Texture atlases.
- Pixel-perfect rendering.
- Basic lighting.
- Rendering benchmarks.

## Phase 3 — World and Chunk Foundation
- Block registry.
- Chunk data.
- Chunk loading.
- Chunk meshing.
- World storage.
- Deterministic generation.
- Chunk modification tracking.
- Sparse persistence.

## Phase 4 — Optimization Foundation
- Chunk cache.
- Memory budgets.
- Automatic unloading.
- Background generation.
- Background meshing.
- Storage cleanup.
- Save validation.
- Profiling overlay.

## Phase 5 — Player Gameplay
- Movement.
- Physics.
- Collision.
- Block breaking.
- Block placement.
- First-person camera.

## Phase 6 — Core Gameplay
- Inventory.
- Items.
- Crafting.
- Health.
- Entities.
- Basic AI.

## Phase 7 — World Generation
- Terrain.
- Biomes.
- Caves.
- Ores.
- Structures.
- Vegetation.
- Fluids.

## Phase 8 — Advanced Gameplay
- Redstone.
- Advanced block states.
- Entity AI.
- Dimensions.
- Technical mechanics.

## Phase 9 — Presentation
- Audio.
- UI.
- Particles.
- Animation.
- Lighting improvements.
- Environmental effects.

## Phase 10 — Cross-Platform Stabilization
- Windows testing.
- macOS testing.
- Save compatibility.
- Performance testing.
- Storage testing.
- Memory testing.
- Packaging.
- Regression testing.

## Phase 11 — Long-Term Expansion
- Additional reference mechanics.
- Further optimization.
- Advanced gameplay systems.
- Additional content.
- Multiplayer if approved.

The roadmap may evolve, but changes must be documented.

---

# 26. DEVELOPMENT RULES

Follow these rules throughout development.

1. Maintain one shared cross-platform codebase.
2. Preserve Windows compatibility.
3. Preserve macOS compatibility.
4. Never modify the original Minecraft installation.
5. Never use original Minecraft assets.
6. Never copy proprietary source code.
7. Create original models, textures, and animations.
8. Maintain 16×16 standard block and item textures.
9. Use the existing OpenSCAD installation.
10. Keep editable `.scad` files.
11. Optimize systems from the beginning.
12. Measure performance improvements.
13. Avoid unnecessary disk storage.
14. Avoid unnecessary memory usage.
15. Avoid unnecessary CPU and GPU work.
16. Use deterministic generation where appropriate.
17. Never delete unsaved or player-created data.
18. Never perform destructive storage operations without safeguards.
19. Keep the project modular.
20. Avoid unnecessary dependencies.
21. Test all significant features.
22. Keep the game buildable throughout development.
23. Update README.md.
24. Update ROADMAP.md.
25. Update ARCHITECTURE.md.
26. Update PERFORMANCE.md.
27. Update CHANGELOG.md.
28. Preserve existing functionality.
29. Never claim untested functionality is complete.
30. Explain significant architectural changes.

Before major implementation work, identify affected systems, dependencies, and potential risks.

After implementation, build, test, benchmark where relevant, and update documentation.

---

# 27. INITIAL ASSIGNMENT

Do not immediately begin implementing the entire game.

Your first assignment is discovery and architecture.

Perform these tasks in order:

1. Inspect the development machine.
2. Identify the current operating system and architecture.
3. Identify available development tools.
4. Locate the Minecraft Java Edition installation.
5. Identify the most recently installed version.
6. Inspect available reference metadata and configurations.
7. Investigate the existing OpenSCAD installation.
8. Evaluate Rust and C++.
9. Evaluate cross-platform graphics technologies.
10. Recommend a technology stack.
11. Propose the shared Windows/macOS architecture.
12. Design the rendering architecture.
13. Design the chunk lifecycle.
14. Design deterministic world generation.
15. Design sparse chunk persistence.
16. Design safe chunk cleanup.
17. Design memory-management policies.
18. Design the texture pipeline.
19. Design the OpenSCAD model pipeline.
20. Design the animation pipeline.
21. Establish initial performance targets.
22. Propose the directory structure.
23. Create the required documentation.
24. Present your findings.

Do not install dependencies or make substantial changes to the machine without my approval.

Do not begin substantial game implementation until I approve the initial plan.

---

# FINAL OBJECTIVE

Create an independently developed, cross-platform recreation of the selected Minecraft Java Edition version.

It must run natively on Windows and macOS.

It must have its own engine, renderer, world system, assets, models, animations, sounds, gameplay mechanics, and save format.

It must preserve the recognizable block-based gameplay experience while using original implementations and independently produced assets.

It must be designed for performance and resource efficiency.

The game should avoid storing data that can be safely regenerated, avoid retaining resources that are no longer needed, minimize unnecessary calculations, and make efficient use of available hardware.

**Do not just make the game work. Make it work efficiently, reliably, and consistently across both supported operating systems.**

Prioritize correctness, performance, maintainability, and data integrity together.

This is a long-term project. Build it accordingly.