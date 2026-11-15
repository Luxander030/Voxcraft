// Chunk column: 16x256x16 blocks in 16 sections, plus light, biomes and persistent extras.
//
// Memory: a section is either "uniform" (one state, no allocation - typical for air above
// terrain and solid stone deep below) or a 8 KiB array. Section arrays and light arrays are
// held through shared_ptr and copied on write, so background mesh/light jobs can take cheap
// immutable snapshots without locking the chunk.
//
// Modification tracking (see ARCHITECTURE.md "Sparse persistence"): every block write after
// generation goes through Chunk::set(), which maintains `delta`: column index -> ORIGINAL
// generated state. Writing the original back removes the entry, so a chunk whose edits were
// all reverted has an empty delta and is again fully reproducible from the generator.
#pragma once
#include "core/common.h"
#include "world/block.h"
#include <memory>
#include <unordered_map>
#include <atomic>

namespace vx {

struct SectionData {
    BlockState b[4096];
    u16 nonAir = 0;
};

struct Section {
    std::shared_ptr<SectionData> data;
    BlockState uniform = AIR;
    static int idx(int x, int y, int z) { return (y << 8) | (z << 4) | x; }
    BlockState get(int i) const { return data ? data->b[i] : uniform; }
    BlockState get(int x, int y, int z) const { return get(idx(x, y, z)); }
    bool isEmpty() const { return !data && uniform == AIR; }
    bool isUniform() const { return !data; }
    void set(int i, BlockState s);
    void fill(BlockState s) { data.reset(); uniform = s; }
    void compact();   // collapse to uniform if every entry is equal
};

// One byte per block: high nibble sky light, low nibble block light.
struct LightData { u8 v[4096]; };
struct LightSection {
    std::shared_ptr<LightData> data;
    u8 uniform = 0xF0;   // full sky, no block light
    u8 get(int i) const { return data ? data->v[i] : uniform; }
};

// Persistent item stack (ids are registry indices at runtime; stored via name palette on disk).
struct ItemStack {
    u16 item = 0;    // 0 = empty
    u8 count = 0;
    u16 damage = 0;
    bool empty() const { return item == 0 || count == 0; }
    bool operator==(const ItemStack& o) const { return item == o.item && count == o.count && damage == o.damage; }
};

struct BlockEntity {
    u16 index = 0;                 // column index
    u8 kind = 0;                   // 1 = chest, 2 = kiln
    std::vector<ItemStack> items;
    i32 data[4] = {0, 0, 0, 0};    // kiln: burn left, burn total, cook progress, unused
};
enum : u8 { BE_NONE = 0, BE_CHEST = 1, BE_KILN = 2, BE_HOPPER = 3, BE_DROPPER = 4 };   // dispensers use BE_DROPPER

struct ScheduledTick {
    u16 index;          // column index
    BlockState block;   // block expected at the position when the tick fires
    i64 due;            // absolute world tick
    u8 priority;
};

// Serialized entity (type name + payload) waiting in an unloaded/just-loaded chunk.
struct EntityRecord { std::string type; std::vector<u8> payload; };

class Chunk {
public:
    explicit Chunk(ChunkPos p) : pos(p) {}
    ChunkPos pos;
    Section sec[SECTIONS];
    LightSection light[SECTIONS];
    u8 biome[256] = {};                 // per column (x + z*16)
    u8 heightmap[256] = {};             // highest light-blocking y + 1 (computed by lighting)

    // persistent state beyond the generated baseline
    std::unordered_map<u16, BlockState> delta;      // column index -> original generated state
    std::vector<BlockEntity> blockEntities;
    std::vector<ScheduledTick> ticks;
    std::vector<EntityRecord> entities;              // serialized; filled on save/unload

    // bookkeeping
    u32 blockVersion = 0;          // bumps on every block change (mesh/light invalidation)
    u32 lightVersion = 0;
    bool unsaved = false;          // persistent state changed since last save was encoded
    bool hasRecord = false;        // a record exists on disk or pending commit
    u64 recordHash = 0;            // hash of the last encoded record (skip identical rewrites)

    BlockState get(int lx, int y, int lz) const {
        if ((unsigned)y >= (unsigned)WORLD_H) return AIR;
        return sec[y >> 4].get(lx, y & 15, lz);
    }
    // Generation-time write: defines the baseline, never tracked.
    void setGen(int lx, int y, int lz, BlockState s) { if ((unsigned)y < (unsigned)WORLD_H) sec[y >> 4].set(Section::idx(lx, y & 15, lz), s); }
    // Gameplay write: tracked against the generated baseline. Returns false if unchanged.
    bool set(int lx, int y, int lz, BlockState s);
    // Apply a stored delta on load (original from regenerated baseline).
    void applyDelta(u16 index, BlockState s);

    u8 skyLight(int lx, int y, int lz) const { if (y >= WORLD_H) return 15; if (y < 0) return 0; return light[y >> 4].get(Section::idx(lx, y & 15, lz)) >> 4; }
    u8 blockLight(int lx, int y, int lz) const { if ((unsigned)y >= (unsigned)WORLD_H) return 0; return light[y >> 4].get(Section::idx(lx, y & 15, lz)) & 15; }

    BlockEntity* blockEntity(u16 index);
    BlockEntity& ensureBlockEntity(u16 index, u8 kind);
    void removeBlockEntity(u16 index);

    // True if the chunk holds anything that cannot be regenerated.
    bool hasPersistentData() const { return !delta.empty() || !blockEntities.empty() || !ticks.empty() || !entities.empty(); }
    void compact() { for (auto& s : sec) s.compact(); }
    size_t memoryBytes() const;
    int topSection() const { for (int i = SECTIONS - 1; i >= 0; i--) if (!sec[i].isEmpty()) return i; return -1; }
};

using ChunkPtr = std::shared_ptr<Chunk>;

} // namespace vx
