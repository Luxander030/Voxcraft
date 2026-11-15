# Changelog

## Unreleased
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
