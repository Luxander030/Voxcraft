// World: chunk lifecycle, background generation/loading/lighting, block access, scheduled ticks,
// sparse persistence. Single-threaded API (main/simulation thread); heavy work runs on JobSystem.
//
// Chunk lifecycle (ARCHITECTURE.md "Chunk lifecycle"):
//   Unloaded -> Requested -> Generating -> Generated -> (lit) Ready -> [Dirty] -> Saving -> Cached -> Unloaded
// "Meshing" is tracked by the renderer per section. Cached chunks sit in a memory-bounded LRU and
// are always either unmodified or already persisted, so evicting them never loses data.
#pragma once
#include "core/common.h"
#include "core/jobs.h"
#include "storage/storage.h"
#include "world/chunk.h"
#include "world/worldgen.h"
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <queue>

namespace vx {

enum class ChunkState : u8 { Requested, Generating, Generated, Ready, Unloading };
const char* chunkStateName(ChunkState s);

// Implemented by the renderer to react to world changes.
struct WorldListener {
    virtual ~WorldListener() = default;
    virtual void onChunkReady(ChunkPos) {}                       // lit and usable
    virtual void onSectionsChanged(ChunkPos, u16 sectionMask) {} // blocks or light changed in these sections
    virtual void onChunkUnloaded(ChunkPos) {}
    virtual void onBlockChanged(const BlockPos&, BlockState oldS, BlockState newS) {}
};

// Implemented by the entity system so entities persist with their chunk.
struct EntityHost {
    virtual ~EntityHost() = default;
    virtual std::vector<EntityRecord> collectEntities(ChunkPos, bool removeFromWorld) = 0;
    virtual void spawnEntities(ChunkPos, std::vector<EntityRecord>&& recs) = 0;
};

struct MemoryBudget {
    size_t chunkCacheBytes = 96u << 20;   // LRU of unloaded-but-cached chunks
};

enum SetFlags : u32 { SET_NOTIFY = 1, SET_UPDATE_NEIGHBORS = 2, SET_DEFAULT = 3, SET_NO_LIGHT = 4 };

class World {
public:
    World(const BlockRegistry& reg, JobSystem& jobs, ItemNames items);
    ~World();

    // --- lifecycle
    bool create(const fs::path& dir, const WorldMeta& meta, std::string* err);
    bool open(const fs::path& dir, std::string* err);
    void close();                     // saves synchronously, waits for jobs
    bool isOpen() const { return gen_ != nullptr; }

    WorldMeta meta;
    const BlockRegistry& reg;
    CoreBlocks cb;
    WorldStorage& storage() { return storage_; }
    const WorldGenerator& generator() const { return *gen_; }
    ItemNames items;

    void setListener(WorldListener* l) { listener_ = l; }
    void setEntityHost(EntityHost* h) { entities_ = h; }
    MemoryBudget budget;

    // --- streaming
    void setFocus(Vec3 pos, int loadRadius);      // chunks within radius are kept loaded (+2 hysteresis)
    void update(double budgetMs = 4.0);            // main-thread: schedule work, apply finished jobs
    ChunkPtr chunk(ChunkPos p) const;              // Generated or Ready, else null
    bool isReady(ChunkPos p) const;
    ChunkPtr loadChunkSync(ChunkPos p);            // generate+load+light on the calling thread (tools/tests)
    void waitForJobs();

    // --- blocks
    BlockState getBlock(int x, int y, int z) const;
    BlockState getBlock(const BlockPos& p) const { return getBlock(p.x, p.y, p.z); }
    bool setBlock(const BlockPos& p, BlockState s, u32 flags = SET_DEFAULT);
    u8 skyLight(const BlockPos& p) const;
    u8 blockLight(const BlockPos& p) const;
    int surfaceY(int x, int z) const;               // top non-air y, from loaded data (or -1)
    BlockEntity* blockEntity(const BlockPos& p);
    BlockEntity* ensureBlockEntity(const BlockPos& p, u8 kind);

    // --- simulation (20 ticks per second)
    void tick();
    void scheduleTick(const BlockPos& p, BlockState expect, int delay, u8 priority = 0);
    i64 currentTick() const { return meta.tick; }
    std::function<void(const BlockPos&, BlockState)> onScheduledTick;   // dispatches to block behaviours
    std::function<void(const BlockPos&, const BlockPos& from)> onNeighborChanged;
    std::function<void(const BlockPos&, BlockState)> onRandomTick;
    int simulationRadius = 6;   // chunks

    // --- persistence
    bool save(bool sync, const std::map<std::string, std::vector<u8>>& extras, std::string* err = nullptr);
    bool saveInProgress() const { return saving_.load(); }

    // --- stats
    struct Stats {
        int requested = 0, generating = 0, generated = 0, ready = 0, cached = 0, dirty = 0, withData = 0;
        size_t chunkBytes = 0, cacheBytes = 0; int pendingWrites = 0; int lightQueue = 0;
    };
    Stats stats() const;
    template <class F> void forEachChunk(F&& f) const { for (auto& [p, s] : slots_) if (s.chunk && s.state >= ChunkState::Generated) f(p, *s.chunk, s.state); }

private:
    struct Slot {
        ChunkState state = ChunkState::Requested;
        ChunkPtr chunk;
        CancelToken cancel;
        bool lightQueued = false, lightDirty = false;
        u16 lightDirtyMask = 0xFFFF;
    };
    void requestChunk(ChunkPos p);
    void onGenerated(ChunkPos p, ChunkPtr c);
    void tryLight(ChunkPos p);
    void queueLight(ChunkPos p);
    void unloadChunk(ChunkPos p);
    void markLightDirty(const BlockPos& p);
    ChunkPtr buildChunk(ChunkPos p);              // generate + apply stored record (thread-safe)
    std::vector<u8> encodeForSave(Chunk& c);
    void cachePut(ChunkPtr c);
    ChunkPtr cacheTake(ChunkPos p);
    void runScheduledTicks();
    void runRandomTicks();

    JobSystem& jobs_;
    WorldStorage storage_;
    std::unique_ptr<WorldGenerator> gen_;
    WorldListener* listener_ = nullptr;
    EntityHost* entities_ = nullptr;
    std::unordered_map<ChunkPos, Slot, ChunkPosHash> slots_;
    // LRU cache of unloaded chunks
    std::list<ChunkPtr> lru_;
    std::unordered_map<ChunkPos, std::list<ChunkPtr>::iterator, ChunkPosHash> lruIndex_;
    size_t lruBytes_ = 0;
    // records encoded but not yet durably committed (empty = delete). Read before disk.
    std::mutex pendingM_;
    std::unordered_map<ChunkPos, std::vector<u8>, ChunkPosHash> pending_;
    std::atomic<bool> saving_{false};
    // streaming focus (read by job priority functions on worker threads)
    std::atomic<float> focusX_{0}, focusZ_{0};
    ChunkPos focusChunk_{0, 0};
    int loadRadius_ = 8;
    bool focusSet_ = false;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
    Rng tickRng_{12345};
};

// Light computation for one chunk from a 3x3 neighbourhood snapshot (exposed for tests/benchmarks).
struct LightSnapshot { Section sec[9][SECTIONS]; ChunkPos center; };
void computeChunkLight(const LightSnapshot& snap, const BlockRegistry& reg, LightSection out[SECTIONS], u8 heightmap[256]);

} // namespace vx
