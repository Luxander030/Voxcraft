# Contributing

## Code
- C++20. Use the `vx::` namespace and 4-space indent. Keep headers small, and put a comment at
  the top of each file explaining *why* the module is shaped the way it is.
- No exceptions on hot paths. Report errors through `bool` + `std::string* err`.
- Do not allocate per block or per frame on hot paths. Reuse buffers.
- Do not touch the filesystem or sleep on the main thread.
- World-generation code must stay deterministic. No `std::rand`, no unordered-container
  iteration order, no transcendental math (`sin`, `exp`, `pow`), no globals. Any change to
  generated output needs a new generator version (the golden test enforces this).

## Data integrity
- Every gameplay block write goes through `World::setBlock` → `Chunk::set`.
- Any new persistent per-chunk data must be included in `hasPersistentData()`, in the record
  codec, and in `storage_*` tests.
- Destructive maintenance needs a dry run and a backup.

## Tests
Persistence changes must keep `persistence_stress_*` green. It is the broadest data-integrity
check and is verified to catch the chunk-cache entity-loss regression.

`./build/voxtests [filter]`. Add a `TEST(name)` next to related tests. Storage changes must keep
Tests A–F green.

## Assets
See ASSET_PIPELINE.md. Only original work is accepted. Use 16×16 for standard block and item
textures.
