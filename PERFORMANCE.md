# Performance

## Targets

| Metric | Target |
|---|---|
| Frame time | ≤ 8.3 ms at 12-chunk render distance on Apple M1; ≤ 16.6 ms on integrated Intel/AMD |
| Chunk generation | ≤ 2 ms per chunk, single thread |
| Chunk lighting | ≤ 2 ms per chunk |
| Section mesh | ≤ 0.5 ms average |
| Main-thread spikes | none > 2 ms from streaming (budgeted pump) |
| Resident memory per loaded chunk (blocks + light) | ≤ 64 KiB average |
| Save of a world with N modified chunks | O(N), independent of chunks visited |
| Unmodified chunk on disk | 0 bytes |

## Benchmark methodology

- Build Release: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`.
- Run `./build/voxbench [chunks]`. It reports the median per-operation time over N runs on one
  thread, with fixed seed 12345.
- Compare runs only on the same machine and power state.

## Results

macOS 15.5, Apple Silicon (arm64), Release, 2026-11-15, 256 chunks:

| Operation | Median |
|---|---|
| Generate chunk | 0.461 ms |
| Light chunk (3×3 snapshot) | 0.683 ms |
| Resident size | 42.2 KiB/chunk (uniform sections cost nothing) |
| Encode sparse record (64 edits) | 0.002 ms |
| Decode sparse record | 0.002 ms |
| Stored size, 64 edits | **36 B sparse vs 3517 B full** (≈ 98% smaller) |

Storage tests (`voxtests`):
- An unmodified chunk produces 0 bytes and no region file.
- Visiting ~100 chunks and editing one stores exactly 1 record.

### Rendering (macOS arm64, Release, render distance 10, vsync on)

| Metric | Value |
|---|---|
| Mesh one chunk (all 16 sections, worker thread) | 0.56 ms; 5 non-empty sections, 1727 quads, 81 KiB |
| Frame time in `--smoke` runs | 16.6 ms (vsync-bound) |
| Loaded chunks / mesh sections | ~420 / ~1400 |
| Visible sections after frustum culling | ~460 → ~600 draw calls |
| Triangles drawn | ~190–230 k |
| Mesh VRAM | ~15 MiB (12 B/vertex) |
| Process RSS | 85–125 MiB |

Audio bank: 51 sounds synthesized in 18 ms at startup, 1.6 MiB of RAM, 0 bytes on disk.

### Occlusion (cave) culling: section connectivity graph

Same world and seed, render distance 10, Release, macOS arm64; `--no-occlusion` vs default:

| Scene | Visible sections | Draw calls | Triangles |
|---|---|---|---|
| Underground pocket (y = 24) | 279 → **4** | 341 → **7** | 113 k → **1.3 k** (−99%) |
| Surface, hilly | 286 → **152** | 333 → **270** | 148 k → **109 k** (−26%) |

The cost is a connectivity flood fill per meshed section, which runs on worker threads and adds
about 0.2 ms per chunk, plus a BFS over the visible sections on the main thread. The frame time
on this Mac is vsync-bound (16.7 ms) with or without culling, so the gain is GPU headroom. It
matters most on integrated GPUs and at long render distances. F6 toggles culling for comparison.

### Distant terrain (LOD)
- About 10,180 quads for a 160–480 block ring, built in 8.5 ms on a worker thread from generator
  column heights. No chunk generation or loading is needed, and it costs one draw call.
- It is rebuilt only after 64 blocks of movement or a change in render distance. Each job uses
  its own generator instance, so a closing world can never be referenced.

### Lighting: uniform sections, emitter list, bounded sky seeding
| | Before | After |
|---|---|---|
| `voxbench` light, median of 3 runs | 0.73–0.84 ms/chunk | **0.51–0.53 ms/chunk** (≈ −33%) |

Output is bit-identical before and after (`lighting_output_is_pinned` has the same hash for both
implementations). The three changes:
- Uniform sections fill opacity and emission without per-cell lookups.
- Light sources are collected during the scan instead of rescanning the whole region.
- Sky-light seeding stops at the region's highest light-blocking block.

Relights already report only the sections whose light actually changed, so a block edit
re-meshes only those sections.

### Region cache bound
`WorldStorage` keeps at most 64 parsed region files (LRU; `maxCachedRegions`). Evicting is
always safe, because the cache only ever mirrors committed data and the World reads its pending
writes before going to disk (`storage_region_cache_is_bounded_and_eviction_is_lossless`).

Greedy merging turns a flat 16×16 floor from 512 quads into 2 (`render_greedy_merges_flat_floor`).

## Storage optimizations in place

- Sparse delta records. Unmodified or reverted chunks are not stored, and empty region files are
  deleted.
- Records carry their own name palette, so they survive registry changes.
- LZ4 is used only when it actually shrinks the record (> 96 B and a net saving).
- Unchanged records are not rewritten (`recordHash`).
- Region files group 32×32 chunks to avoid thousands of tiny files. Only touched regions are
  rewritten at save.

## CPU / memory optimizations in place

- Uniform-section representation and copy-on-write sections.
- Distance-ranked job queue that re-ranks dynamically, plus cancellation of jobs for chunks that
  went out of range.
- Light is computed per chunk from immutable snapshots with no locks. It is never stored.
- Clean-chunk LRU cache bounded in bytes.

## Known bottlenecks / open items

- Full relight of a chunk on any opacity change. Incremental light updates are planned once the
  renderer can show the difference.
- GPU metrics will be added with the renderer (draw calls, triangles, upload bytes counters
  already exist in `Profiler`).
