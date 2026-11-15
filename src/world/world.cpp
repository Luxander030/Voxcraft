#include "world/world.h"
#include "core/profiler.h"
#include <ctime>

namespace vx {

const char* chunkStateName(ChunkState s) {
    switch (s) { case ChunkState::Requested: return "requested"; case ChunkState::Generating: return "generating";
        case ChunkState::Generated: return "generated"; case ChunkState::Ready: return "ready"; case ChunkState::Unloading: return "unloading"; }
    return "?";
}

static std::mutex g_commitM;   // serialises commits (async autosave vs. sync save)

World::World(const BlockRegistry& r, JobSystem& jobs, ItemNames it) : reg(r), items(std::move(it)), jobs_(jobs) { cb.resolve(r); }

World::~World() { close(); }

bool World::create(const fs::path& dir, const WorldMeta& m, std::string* err) {
    std::error_code ec;
    if (fs::exists(dir / "world.json", ec)) { if (err) *err = "world already exists"; return false; }
    if (!storage_.open(dir, true, err)) return false;
    meta = m;
    meta.createdUnix = meta.lastPlayedUnix = (u64)time(nullptr);
    gen_ = createGenerator(meta.gen, reg);
    if (!gen_) { if (err) *err = "unknown generator version"; return false; }
    // spawn point: surface at origin area (pure function of the generator)
    if (meta.spawn.y == 80 && meta.spawn.x == 0 && meta.spawn.z == 0) {
        for (int r = 0; r < 64; r += 4) {
            ColumnInfo ci = gen_->column(r, r / 2);
            if (ci.height >= SEA_LEVEL) { meta.spawn = {r, ci.height + 1, r / 2}; break; }
        }
    }
    WorldStorage::Batch b; b.meta = &meta;
    return storage_.commit(b, err);
}

bool World::open(const fs::path& dir, std::string* err) {
    if (!storage_.open(dir, false, err)) return false;
    if (!storage_.loadMeta(meta, err)) return false;
    gen_ = createGenerator(meta.gen, reg);
    if (!gen_) { if (err) *err = "world uses unknown generator version " + std::to_string(meta.gen.version) + " - refusing to open (terrain would change)"; return false; }
    return true;
}

void World::close() {
    if (!gen_) return;
    std::string err;
    if (!save(true, {}, &err)) VX_ERROR("final save failed: %s", err.c_str());
    for (auto& [p, s] : slots_) if (s.cancel) s.cancel->store(true);
    alive_->store(false);
    jobs_.waitIdle();
    jobs_.pumpMain();   // drain closures (they check alive_)
    slots_.clear(); lru_.clear(); lruIndex_.clear(); lruBytes_ = 0;
    gen_.reset();
}

// ------------------------------------------------------------------ chunk building (worker-safe)
ChunkPtr World::buildChunk(ChunkPos p) {
    auto c = std::make_shared<Chunk>(p);
    gen_->generate(*c);
    std::vector<u8> rec;
    bool havePending = false;
    { std::lock_guard<std::mutex> lk(pendingM_); auto it = pending_.find(p); if (it != pending_.end()) { rec = it->second; havePending = true; } }
    if (!havePending) rec = storage_.readRecord(p);
    if (!rec.empty()) {
        VX_TIME_US(Stat::LoadTimeUs);
        std::string err;
        if (!decodeChunkRecord(rec.data(), rec.size(), *c, reg, items, meta.tick, nullptr, &err)) {
            VX_ERROR("chunk %d,%d record corrupt (%s); keeping stored record untouched, chunk is read-only regenerated", p.x, p.z, err.c_str());
        }
        c->hasRecord = true;
        c->recordHash = fnv1a(rec.data(), rec.size());
        c->compact();
        Profiler::add(Stat::ChunksLoadedFromDisk);
    }
    return c;
}

ChunkPtr World::loadChunkSync(ChunkPos p) {
    auto it = slots_.find(p);
    if (it != slots_.end() && it->second.chunk && it->second.state >= ChunkState::Generated) {
        if (it->second.state == ChunkState::Ready) return it->second.chunk;
    } else {
        if (it != slots_.end() && it->second.cancel) it->second.cancel->store(true);
        ChunkPtr c = cacheTake(p);
        if (!c) c = buildChunk(p);
        slots_[p] = Slot{}; onGenerated(p, c);
    }
    // light synchronously using whatever neighbours exist (missing neighbours treated as empty sky)
    Slot& s = slots_[p];
    LightSnapshot snap; snap.center = p;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        ChunkPtr n = chunk({p.x + dx, p.z + dz});
        if (n) for (int i = 0; i < SECTIONS; i++) snap.sec[(dx + 1) + (dz + 1) * 3][i] = n->sec[i];
    }
    computeChunkLight(snap, reg, s.chunk->light, s.chunk->heightmap);
    s.state = ChunkState::Ready; s.lightDirty = false;
    return s.chunk;
}

void World::waitForJobs() {
    // Drive update() until streaming has settled: no chunk waiting on generation or lighting and
    // no main-thread results left to apply. Used by tools/tests; the game never blocks like this.
    for (int i = 0; i < 10000; i++) {
        update(0);
        jobs_.waitIdle();
        int applied = jobs_.pumpMain();
        bool busy = false;
        for (auto& [p, s] : slots_)
            if (s.state < ChunkState::Generated || s.lightQueued || (s.lightDirty && s.state >= ChunkState::Generated)) { busy = true; break; }
        if (!busy && applied == 0) { update(0); if (jobs_.pumpMain() == 0) break; }
    }
}

// ------------------------------------------------------------------ streaming
void World::setFocus(Vec3 pos, int radius) {
    focusX_.store(pos.x); focusZ_.store(pos.z);
    focusChunk_ = {(int)std::floor(pos.x) >> 4, (int)std::floor(pos.z) >> 4};
    loadRadius_ = std::max(2, radius);
    focusSet_ = true;
}

void World::requestChunk(ChunkPos p) {
    if (ChunkPtr c = cacheTake(p)) { slots_[p] = Slot{}; onGenerated(p, c); slots_[p].lightDirty = true; return; }
    Slot s; s.state = ChunkState::Requested; s.cancel = makeCancelToken();
    CancelToken tok = s.cancel;
    slots_[p] = s;
    auto alive = alive_;
    jobs_.submit([this, p, tok, alive] {
        ChunkPtr c = buildChunk(p);
        jobs_.postMain([this, p, c, tok, alive] {
            if (!alive->load() || tok->load()) return;
            auto it = slots_.find(p);
            if (it == slots_.end() || it->second.cancel != tok) return;
            onGenerated(p, c);
        });
    }, [this, p] { float dx = (float)(p.x * 16 + 8) - focusX_.load(), dz = (float)(p.z * 16 + 8) - focusZ_.load(); return dx * dx + dz * dz; }, tok);
    slots_[p].state = ChunkState::Generating;
}

void World::onGenerated(ChunkPos p, ChunkPtr c) {
    Slot& s = slots_[p];
    s.chunk = c; s.state = ChunkState::Generated; s.cancel.reset();
    if (entities_ && !c->entities.empty()) { entities_->spawnEntities(p, std::move(c->entities)); c->entities.clear(); }
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) tryLight({p.x + dx, p.z + dz});
}

void World::tryLight(ChunkPos p) {
    auto it = slots_.find(p);
    if (it == slots_.end() || !it->second.chunk || it->second.state < ChunkState::Generated) return;
    if (it->second.state == ChunkState::Ready && !it->second.lightDirty) return;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        if (!dx && !dz) continue;
        auto n = slots_.find({p.x + dx, p.z + dz});
        if (n == slots_.end() || n->second.state < ChunkState::Generated) return;
    }
    queueLight(p);
}

void World::queueLight(ChunkPos p) {
    Slot& s = slots_[p];
    if (s.lightQueued) { s.lightDirty = true; return; }
    s.lightQueued = true; s.lightDirty = false;
    auto snap = std::make_shared<LightSnapshot>(); snap->center = p;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        ChunkPtr n = chunk({p.x + dx, p.z + dz});
        if (n) for (int i = 0; i < SECTIONS; i++) snap->sec[(dx + 1) + (dz + 1) * 3][i] = n->sec[i];
    }
    ChunkPtr c = s.chunk;
    auto alive = alive_;
    jobs_.submit([this, p, c, snap, alive] {
        struct Result { LightSection light[SECTIONS]; u8 hm[256]; };
        auto res = std::make_shared<Result>();
        computeChunkLight(*snap, reg, res->light, res->hm);
        jobs_.postMain([this, p, c, res, alive] {
            if (!alive->load()) return;
            auto it = slots_.find(p);
            if (it == slots_.end() || it->second.chunk != c) return;
            Slot& sl = it->second;
            u16 mask = 0;
            for (int i = 0; i < SECTIONS; i++) {
                const LightSection& a = c->light[i]; const LightSection& b = res->light[i];
                bool same = (!a.data && !b.data) ? a.uniform == b.uniform
                          : (a.data && b.data) ? std::memcmp(a.data->v, b.data->v, 4096) == 0 : false;
                if (!same) mask |= (u16)(1u << i);
                c->light[i] = b;
            }
            std::memcpy(c->heightmap, res->hm, 256);
            c->lightVersion++;
            sl.lightQueued = false;
            bool first = sl.state != ChunkState::Ready;
            sl.state = ChunkState::Ready;
            if (listener_) { if (first) listener_->onChunkReady(p); else if (mask) listener_->onSectionsChanged(p, mask); }
            if (sl.lightDirty) { sl.lightDirty = false; queueLight(p); }
        });
    }, [this, p] { float dx = (float)(p.x * 16 + 8) - focusX_.load(), dz = (float)(p.z * 16 + 8) - focusZ_.load(); return dx * dx + dz * dz - 64.0f; });
}

void World::update(double budgetMs) {
    jobs_.pumpMain(budgetMs);
    if (!gen_ || !focusSet_) return;
    const int r = loadRadius_, r2 = r * r;
    // request missing chunks in the load circle
    for (int dz = -r; dz <= r; dz++) for (int dx = -r; dx <= r; dx++) {
        if (dx * dx + dz * dz > r2) continue;
        ChunkPos p{focusChunk_.x + dx, focusChunk_.z + dz};
        if (!slots_.count(p)) requestChunk(p);
    }
    // unload beyond radius + hysteresis
    const int ur = r + 2, ur2 = ur * ur;
    std::vector<ChunkPos> drop;
    for (auto& [p, s] : slots_) {
        int dx = p.x - focusChunk_.x, dz = p.z - focusChunk_.z;
        if (dx * dx + dz * dz > ur2) drop.push_back(p);
    }
    for (ChunkPos p : drop) unloadChunk(p);
    // relight chunks invalidated by block changes
    for (auto& [p, s] : slots_) if (s.lightDirty && !s.lightQueued && s.state >= ChunkState::Generated) tryLight(p);
}

void World::unloadChunk(ChunkPos p) {
    auto it = slots_.find(p);
    if (it == slots_.end()) return;
    Slot& s = it->second;
    if (s.cancel) s.cancel->store(true);
    if (s.chunk && s.state >= ChunkState::Generated) {
        Chunk& c = *s.chunk;
        if (entities_) c.entities = entities_->collectEntities(p, true);
        std::vector<u8> rec = encodeForSave(c);
        bool dataChanged = !(rec.empty() && !c.hasRecord) && (rec.empty() ? c.hasRecord : fnv1a(rec.data(), rec.size()) != c.recordHash);
        if (dataChanged) {
            std::lock_guard<std::mutex> lk(pendingM_);
            pending_[p] = rec;
            c.hasRecord = !rec.empty();
            c.recordHash = rec.empty() ? 0 : fnv1a(rec.data(), rec.size());
        }
        c.unsaved = false;
        c.entities.clear();   // now persisted in the record
        cachePut(s.chunk);
        Profiler::add(Stat::ChunksUnloaded);
    }
    slots_.erase(it);
    if (listener_) listener_->onChunkUnloaded(p);
}

ChunkPtr World::chunk(ChunkPos p) const {
    auto it = slots_.find(p);
    if (it == slots_.end() || it->second.state < ChunkState::Generated) return nullptr;
    return it->second.chunk;
}
bool World::isReady(ChunkPos p) const { auto it = slots_.find(p); return it != slots_.end() && it->second.state == ChunkState::Ready; }

// ------------------------------------------------------------------ LRU cache
void World::cachePut(ChunkPtr c) {
    if (budget.chunkCacheBytes == 0) return;
    if (auto it = lruIndex_.find(c->pos); it != lruIndex_.end()) { lruBytes_ -= (*it->second)->memoryBytes(); lru_.erase(it->second); lruIndex_.erase(it); }
    lru_.push_front(c); lruIndex_[c->pos] = lru_.begin(); lruBytes_ += c->memoryBytes();
    while (lruBytes_ > budget.chunkCacheBytes && !lru_.empty()) {
        ChunkPtr old = lru_.back(); lru_.pop_back(); lruIndex_.erase(old->pos); lruBytes_ -= old->memoryBytes();
    }
}
ChunkPtr World::cacheTake(ChunkPos p) {
    auto it = lruIndex_.find(p);
    if (it == lruIndex_.end()) return nullptr;
    ChunkPtr c = *it->second; lruBytes_ -= c->memoryBytes(); lru_.erase(it->second); lruIndex_.erase(it);
    return c;
}

// ------------------------------------------------------------------ blocks
BlockState World::getBlock(int x, int y, int z) const {
    if ((unsigned)y >= (unsigned)WORLD_H) return AIR;
    auto it = slots_.find({x >> 4, z >> 4});
    if (it == slots_.end() || !it->second.chunk || it->second.state < ChunkState::Generated) return AIR;
    return it->second.chunk->get(x & 15, y, z & 15);
}

u8 World::skyLight(const BlockPos& p) const {
    if (p.y >= WORLD_H) return 15; if (p.y < 0) return 0;
    ChunkPtr c = chunk(p.chunk()); return c ? c->skyLight(p.x & 15, p.y, p.z & 15) : 15;
}
u8 World::blockLight(const BlockPos& p) const { ChunkPtr c = chunk(p.chunk()); return c ? c->blockLight(p.x & 15, p.y, p.z & 15) : 0; }

int World::surfaceY(int x, int z) const {
    ChunkPtr c = chunk({x >> 4, z >> 4}); if (!c) return -1;
    for (int y = WORLD_H - 1; y >= 0; y--) if (c->get(x & 15, y, z & 15) != AIR) return y;
    return -1;
}

BlockEntity* World::blockEntity(const BlockPos& p) {
    ChunkPtr c = chunk(p.chunk()); if (!c || (unsigned)p.y >= (unsigned)WORLD_H) return nullptr;
    return c->blockEntity(columnIndex(p.x & 15, p.y, p.z & 15));
}
BlockEntity* World::ensureBlockEntity(const BlockPos& p, u8 kind) {
    ChunkPtr c = chunk(p.chunk()); if (!c || (unsigned)p.y >= (unsigned)WORLD_H) return nullptr;
    return &c->ensureBlockEntity(columnIndex(p.x & 15, p.y, p.z & 15), kind);
}

void World::markLightDirty(const BlockPos& p) {
    int lx = p.x & 15, lz = p.z & 15; ChunkPos cp = p.chunk();
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        // distance from the block to the neighbour chunk along each axis must be <= 14 for light to reach it
        int ddx = dx < 0 ? lx + 1 : dx > 0 ? 16 - lx : 0;
        int ddz = dz < 0 ? lz + 1 : dz > 0 ? 16 - lz : 0;
        if (ddx + ddz > 15) continue;
        auto it = slots_.find({cp.x + dx, cp.z + dz});
        if (it != slots_.end() && it->second.state >= ChunkState::Generated) it->second.lightDirty = true;
    }
}

bool World::setBlock(const BlockPos& p, BlockState s, u32 flags) {
    if ((unsigned)p.y >= (unsigned)WORLD_H) return false;
    ChunkPos cp = p.chunk();
    auto it = slots_.find(cp);
    if (it == slots_.end() || !it->second.chunk || it->second.state < ChunkState::Generated) return false;
    Chunk& c = *it->second.chunk;
    int lx = p.x & 15, lz = p.z & 15;
    BlockState old = c.get(lx, p.y, lz);
    if (!c.set(lx, p.y, lz, s)) return false;
    // block entity no longer matches the block -> drop it (caller already spilled contents)
    if (reg.def(old).has(BF_BLOCK_ENTITY) && reg.def(old).index != reg.def(s).index) c.removeBlockEntity(columnIndex(lx, p.y, lz));
    if (!(flags & SET_NO_LIGHT) && (reg.opacity(old) != reg.opacity(s) || reg.light(old) != reg.light(s))) markLightDirty(p);
    if (flags & SET_NOTIFY) {
        if (listener_) {
            listener_->onBlockChanged(p, old, s);
            int sy = p.y >> 4;
            u16 mask = (u16)(1u << sy);
            if ((p.y & 15) == 0 && sy > 0) mask |= (u16)(1u << (sy - 1));
            if ((p.y & 15) == 15 && sy < SECTIONS - 1) mask |= (u16)(1u << (sy + 1));
            listener_->onSectionsChanged(cp, mask);
            if (lx == 0) listener_->onSectionsChanged({cp.x - 1, cp.z}, mask);
            if (lx == 15) listener_->onSectionsChanged({cp.x + 1, cp.z}, mask);
            if (lz == 0) listener_->onSectionsChanged({cp.x, cp.z - 1}, mask);
            if (lz == 15) listener_->onSectionsChanged({cp.x, cp.z + 1}, mask);
        }
    }
    if ((flags & SET_UPDATE_NEIGHBORS) && onNeighborChanged) {
        static const BlockPos dirs[6] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
        for (auto& d : dirs) onNeighborChanged(p + d, p);
        onNeighborChanged(p, p);
    }
    return true;
}

// ------------------------------------------------------------------ ticks
void World::scheduleTick(const BlockPos& p, BlockState expect, int delay, u8 priority) {
    ChunkPtr c = chunk(p.chunk()); if (!c || (unsigned)p.y >= (unsigned)WORLD_H) return;
    u16 idx = columnIndex(p.x & 15, p.y, p.z & 15);
    for (auto& t : c->ticks) if (t.index == idx && t.block == expect) return;   // already scheduled
    c->ticks.push_back({idx, expect, meta.tick + std::max(1, delay), priority});
    c->unsaved = true;
}

void World::runScheduledTicks() {
    struct Due { BlockPos p; BlockState b; i64 due; u8 prio; };
    std::vector<Due> due;
    const int sr2 = simulationRadius * simulationRadius;
    for (auto& [cp, s] : slots_) {
        if (!s.chunk || s.state < ChunkState::Generated) continue;
        int dx = cp.x - focusChunk_.x, dz = cp.z - focusChunk_.z;
        if (focusSet_ && dx * dx + dz * dz > sr2) continue;   // frozen outside simulation distance
        auto& ticks = s.chunk->ticks;
        for (size_t i = 0; i < ticks.size();) {
            if (ticks[i].due <= meta.tick) {
                int lx, y, lz; columnIndexDecode(ticks[i].index, lx, y, lz);
                due.push_back({{cp.x * 16 + lx, y, cp.z * 16 + lz}, ticks[i].block, ticks[i].due, ticks[i].priority});
                ticks[i] = ticks.back(); ticks.pop_back(); s.chunk->unsaved = true;
            } else i++;
        }
    }
    std::stable_sort(due.begin(), due.end(), [](const Due& a, const Due& b) { return a.due != b.due ? a.due < b.due : a.prio < b.prio; });
    for (auto& d : due) if (getBlock(d.p) == d.b && onScheduledTick) onScheduledTick(d.p, d.b);
}

void World::runRandomTicks() {
    if (!onRandomTick) return;
    const int sr2 = simulationRadius * simulationRadius;
    for (auto& [cp, s] : slots_) {
        if (!s.chunk || s.state != ChunkState::Ready) continue;
        int dx = cp.x - focusChunk_.x, dz = cp.z - focusChunk_.z;
        if (focusSet_ && dx * dx + dz * dz > sr2) continue;
        for (int si = 0; si < SECTIONS; si++) {
            if (s.chunk->sec[si].isEmpty()) continue;
            for (int k = 0; k < 3; k++) {
                u32 r = tickRng_.nextU32();
                int lx = r & 15, ly = (r >> 4) & 15, lz = (r >> 8) & 15;
                BlockState b = s.chunk->sec[si].get(lx, ly, lz);
                if (b == AIR) continue;
                onRandomTick({cp.x * 16 + lx, si * 16 + ly, cp.z * 16 + lz}, b);
            }
        }
    }
}

void World::tick() {
    meta.tick++;
    meta.dayTime = (meta.dayTime + 1) % 24000;
    runScheduledTicks();
    runRandomTicks();
}

// ------------------------------------------------------------------ persistence
std::vector<u8> World::encodeForSave(Chunk& c) {
    if (!c.hasPersistentData()) return {};
    return encodeChunkRecord(c, reg, items, meta.tick);
}

bool World::save(bool sync, const std::map<std::string, std::vector<u8>>& extras, std::string* err) {
    if (!gen_) return false;
    // 1. encode every chunk whose persistent state differs from what is stored (main thread; sparse = cheap)
    {
        std::lock_guard<std::mutex> lk(pendingM_);
        for (auto& [p, s] : slots_) {
            if (!s.chunk || s.state < ChunkState::Generated) continue;
            Chunk& c = *s.chunk;
            if (entities_) c.entities = entities_->collectEntities(p, false);
            if (!c.unsaved && c.entities.empty() && !c.hasRecord) continue;
            std::vector<u8> rec = encodeForSave(c);
            c.entities.clear();
            if (rec.empty()) {
                if (c.hasRecord) { pending_[p] = {}; c.hasRecord = false; c.recordHash = 0; }   // reverted: record removed
            } else {
                u64 h = fnv1a(rec.data(), rec.size());
                if (!c.hasRecord || h != c.recordHash) { pending_[p] = std::move(rec); c.hasRecord = true; c.recordHash = h; }
            }
            c.unsaved = false;
            Profiler::add(Stat::ChunksSaved);
        }
    }
    meta.lastPlayedUnix = (u64)time(nullptr);
    // 2. snapshot the batch
    auto batch = std::make_shared<std::vector<std::pair<ChunkPos, std::vector<u8>>>>();
    { std::lock_guard<std::mutex> lk(pendingM_); for (auto& kv : pending_) batch->push_back(kv); }
    auto metaCopy = std::make_shared<WorldMeta>(meta);
    auto extraCopy = std::make_shared<std::map<std::string, std::vector<u8>>>(extras);
    auto doCommit = [this, batch, metaCopy, extraCopy](std::string* e) {
        std::lock_guard<std::mutex> lk(g_commitM);
        WorldStorage::Batch b; b.chunks = *batch; b.meta = metaCopy.get(); b.extraFiles = *extraCopy;
        bool ok = storage_.commit(b, e);
        if (ok) {
            std::lock_guard<std::mutex> plk(pendingM_);
            for (auto& [p, rec] : *batch) { auto it = pending_.find(p); if (it != pending_.end() && it->second == rec) pending_.erase(it); }
        }
        return ok;
    };
    if (sync) return doCommit(err);
    if (saving_.exchange(true)) return true;   // a save is already running; pending data is retained for the next one
    auto alive = alive_;
    jobs_.submit([this, doCommit, alive] {
        std::string e;
        if (!doCommit(&e)) VX_ERROR("autosave failed (data kept in memory, will retry): %s", e.c_str());
        saving_.store(false);
    }, [] { return -1e9f; });   // saves outrank everything
    return true;
}

World::Stats World::stats() const {
    Stats st;
    for (auto& [p, s] : slots_) {
        switch (s.state) {
            case ChunkState::Requested: st.requested++; break; case ChunkState::Generating: st.generating++; break;
            case ChunkState::Generated: st.generated++; break; case ChunkState::Ready: st.ready++; break; default: break;
        }
        if (s.chunk) { st.chunkBytes += s.chunk->memoryBytes(); if (s.chunk->unsaved) st.dirty++; if (s.chunk->hasPersistentData()) st.withData++; }
        if (s.lightQueued) st.lightQueue++;
    }
    st.cached = (int)lru_.size(); st.cacheBytes = lruBytes_;
    { std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(pendingM_)); st.pendingWrites = (int)pending_.size(); }
    return st;
}

} // namespace vx
