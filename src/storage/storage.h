// World storage: sparse chunk records, region container files, journaled multi-file saves.
// Format documented in docs/WORLD_FORMAT.md.
#pragma once
#include "core/common.h"
#include "core/json.h"
#include "core/util.h"
#include "world/chunk.h"
#include "world/worldgen.h"
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>

namespace vx {

// Item id <-> name mapping provided by the game layer (storage stays registry-agnostic for items).
struct ItemNames {
    std::function<std::string(u16)> name;
    std::function<u16(const std::string&)> id;
};

constexpr u32 REGION_MAGIC = 0x47525856;   // "VXRG"
constexpr u16 REGION_FORMAT = 1;
constexpr int REGION_SHIFT = 5;            // 32x32 chunks per region file

enum class RecordKind : u8 { Sparse = 1, Full = 2 };

// ---- chunk record codec (self-contained: carries its own name palette)
struct RecordStats { size_t rawBytes = 0, storedBytes = 0; int deltaCount = 0; bool compressed = false; RecordKind kind = RecordKind::Sparse; };
std::vector<u8> encodeChunkRecord(const Chunk& c, const BlockRegistry& reg, const ItemNames& items, i64 worldTick, RecordStats* st = nullptr);
// Legacy/full representation: every block stored. Used by older saves and by tests of migration.
std::vector<u8> encodeChunkRecordFull(const Chunk& c, const BlockRegistry& reg, const ItemNames& items, i64 worldTick);
// Decodes onto `c`, which must already contain the regenerated baseline.
bool decodeChunkRecord(const u8* data, size_t n, Chunk& c, const BlockRegistry& reg, const ItemNames& items, i64 worldTick, RecordKind* kindOut = nullptr, std::string* err = nullptr);

// ---- region container
struct RegionPos { i32 x = 0, z = 0; bool operator<(const RegionPos& o) const { return x != o.x ? x < o.x : z < o.z; } bool operator==(const RegionPos& o) const { return x == o.x && z == o.z; } };
inline RegionPos regionOf(ChunkPos c) { return {c.x >> REGION_SHIFT, c.z >> REGION_SHIFT}; }
inline u16 regionLocal(ChunkPos c) { return (u16)(((c.z & 31) << 5) | (c.x & 31)); }

struct RegionFile {
    RegionPos pos;
    std::map<u16, std::vector<u8>> records;   // local index -> record bytes
    std::vector<u8> serialize() const;
    bool parse(const std::vector<u8>& bytes, std::string* err);
};

// ---- world metadata
struct WorldMeta {
    u32 formatVersion = 1;
    std::string name = "New World";
    GenSettings gen;
    i64 tick = 0;            // total ticks simulated
    i64 dayTime = 1000;      // time of day in ticks
    BlockPos spawn{0, 80, 0};
    std::string gameMode = "survival";
    bool raining = false; i64 weatherTicks = 12000;   // ticks until the weather changes
    std::vector<std::pair<i32, i32>> populatedVillages;   // village regions whose settlers were spawned
    u64 createdUnix = 0, lastPlayedUnix = 0;
    Json toJson() const;
    static bool fromJson(const Json& j, WorldMeta& out, std::string* err);
};

// Fault injection for crash-recovery tests (Test F).
enum class SaveFault { None, AfterNewFiles, AfterJournal, MidRename };

class WorldStorage {
public:
    // Opens (and recovers) a world directory. Creates it when `create` is set.
    bool open(const fs::path& dir, bool create, std::string* err);
    const fs::path& dir() const { return dir_; }

    bool loadMeta(WorldMeta& out, std::string* err);

    // Returns the stored record for a chunk (empty if none). Thread-safe.
    std::vector<u8> readRecord(ChunkPos c);
    bool hasRecord(ChunkPos c);

    // A batch of changes committed atomically: chunk records to write (empty vector = delete record),
    // plus optional meta and player blobs.
    struct Batch {
        std::vector<std::pair<ChunkPos, std::vector<u8>>> chunks;
        const WorldMeta* meta = nullptr;
        std::map<std::string, std::vector<u8>> extraFiles;   // e.g. "player.dat"
    };
    bool commit(const Batch& b, std::string* err);

    std::optional<std::vector<u8>> readExtra(const std::string& name);

    // Diagnostics
    struct Usage { u64 regionFiles = 0, regionBytes = 0, records = 0, totalBytes = 0; };
    Usage usage();
    std::vector<RegionPos> listRegions();
    bool loadRegion(RegionPos rp, RegionFile& out, std::string* err);

    // Recovery report from the last open()
    std::string lastRecovery;

    static SaveFault s_fault;   // test hook

private:
    fs::path regionPath(RegionPos p) const;
    RegionFile* cachedRegion(RegionPos p);   // requires m_
    bool recover(std::string* err);
    fs::path dir_;
    std::mutex m_;
    std::map<RegionPos, RegionFile> cache_;  // parsed region files (records only - small, sparse)
    std::map<RegionPos, bool> missing_;
    std::map<RegionPos, u64> lastUse_;
    u64 useClock_ = 0;
    void trimCache();   // LRU bound; only called where no RegionFile pointers are held
public:
    size_t maxCachedRegions = 64;
    size_t cachedRegionCount() const { return cache_.size(); }
private:
};

} // namespace vx
