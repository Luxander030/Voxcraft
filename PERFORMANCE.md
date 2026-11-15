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

- `WorldStorage` keeps parsed records of every touched region in memory. Records are small, but
  the cache should get an LRU bound for very large edited worlds.
- Full relight of a chunk on any opacity change. Incremental light updates are planned once the
  renderer can show the difference.
- GPU metrics will be added with the renderer (draw calls, triangles, upload bytes counters
  already exist in `Profiler`).
