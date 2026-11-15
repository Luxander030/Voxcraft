#include "storage/maintenance.h"
#include <ctime>

namespace vx {

namespace {
bool sameChunk(const Chunk& a, const Chunk& b) {
    for (int s = 0; s < SECTIONS; s++)
        for (int i = 0; i < 4096; i++) if (a.sec[s].get(i) != b.sec[s].get(i)) return false;
    if (a.blockEntities.size() != b.blockEntities.size()) return false;
    for (size_t i = 0; i < a.blockEntities.size(); i++) {
        const BlockEntity& x = a.blockEntities[i]; const BlockEntity& y = b.blockEntities[i];
        if (x.index != y.index || x.kind != y.kind || x.items != y.items || std::memcmp(x.data, y.data, sizeof x.data)) return false;
    }
    if (a.ticks.size() != b.ticks.size()) return false;
    for (size_t i = 0; i < a.ticks.size(); i++)
        if (a.ticks[i].index != b.ticks[i].index || a.ticks[i].block != b.ticks[i].block || a.ticks[i].due != b.ticks[i].due) return false;
    if (a.entities.size() != b.entities.size()) return false;
    for (size_t i = 0; i < a.entities.size(); i++) if (a.entities[i].type != b.entities[i].type || a.entities[i].payload != b.entities[i].payload) return false;
    return true;
}
} // namespace

fs::path backupWorld(const fs::path& dir, std::string* err) {
    char stamp[32]; time_t t = time(nullptr); struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
    fs::path abs = fs::absolute(dir);
    fs::path out = abs.parent_path() / (abs.filename().string() + "-backup-" + stamp);
    std::error_code ec;
    fs::copy(abs, out, fs::copy_options::recursive, ec);
    if (ec) { if (err) *err = "backup failed: " + ec.message(); return {}; }
    return out;
}

static bool processWorld(const fs::path& dir, const BlockRegistry& reg, const ItemNames& items, bool write, bool dryRun, MaintenanceReport& rep, std::string* err) {
    WorldStorage st;
    if (!st.open(dir, false, err)) return false;
    WorldMeta meta;
    if (!st.loadMeta(meta, err)) return false;
    auto gen = createGenerator(meta.gen, reg);
    if (!gen) { if (err) *err = "unknown generator version"; return false; }
    rep.bytesBefore = st.usage().regionBytes;
    WorldStorage::Batch batch;
    for (RegionPos rp : st.listRegions()) {
        RegionFile rf; std::string rerr;
        if (!st.loadRegion(rp, rf, &rerr)) { rep.log.push_back("region " + std::to_string(rp.x) + "," + std::to_string(rp.z) + ": " + rerr + " (left untouched)"); rep.recordsFailed++; continue; }
        rep.regions++;
        for (auto& [local, rec] : rf.records) {
            rep.records++;
            ChunkPos cp{rp.x * 32 + (local & 31), rp.z * 32 + (local >> 5)};
            Chunk c(cp); gen->generate(c);
            RecordKind kind; std::string derr;
            if (!decodeChunkRecord(rec.data(), rec.size(), c, reg, items, meta.tick, &kind, &derr)) {
                rep.recordsFailed++; rep.log.push_back("chunk " + std::to_string(cp.x) + "," + std::to_string(cp.z) + ": " + derr + " (left untouched)");
                continue;
            }
            if (!write) { rep.recordsKept++; continue; }
            if (!c.hasPersistentData()) {
                rep.recordsRemoved++;
                rep.log.push_back("chunk " + std::to_string(cp.x) + "," + std::to_string(cp.z) + ": identical to generated terrain -> record removable");
                batch.chunks.push_back({cp, {}});
                continue;
            }
            std::vector<u8> sparse = encodeChunkRecord(c, reg, items, meta.tick);
            if (kind == RecordKind::Sparse && sparse.size() >= rec.size()) { rep.recordsKept++; continue; }
            // validate: the new record must reproduce exactly the same chunk
            Chunk check(cp); gen->generate(check);
            if (!decodeChunkRecord(sparse.data(), sparse.size(), check, reg, items, meta.tick, nullptr, &derr) || !sameChunk(c, check)) {
                rep.recordsFailed++; rep.log.push_back("chunk " + std::to_string(cp.x) + "," + std::to_string(cp.z) + ": validation failed, kept original");
                continue;
            }
            rep.recordsMigrated++;
            rep.log.push_back("chunk " + std::to_string(cp.x) + "," + std::to_string(cp.z) + ": " + std::to_string(rec.size()) + " -> " + std::to_string(sparse.size()) + " bytes");
            batch.chunks.push_back({cp, std::move(sparse)});
        }
    }
    if (!write || dryRun || batch.chunks.empty()) { rep.bytesAfter = rep.bytesBefore; return true; }
    rep.backupDir = backupWorld(dir, err);
    if (rep.backupDir.empty()) return false;
    if (!st.commit(batch, err)) return false;   // journaled: all-or-nothing
    rep.bytesAfter = st.usage().regionBytes;
    return true;
}

bool optimizeWorld(const fs::path& dir, const BlockRegistry& reg, const ItemNames& items, bool dryRun, MaintenanceReport& rep, std::string* err) {
    return processWorld(dir, reg, items, true, dryRun, rep, err);
}

bool verifyWorld(const fs::path& dir, const BlockRegistry& reg, const ItemNames& items, MaintenanceReport& rep, std::string* err) {
    bool ok = processWorld(dir, reg, items, false, true, rep, err);
    return ok && rep.recordsFailed == 0;
}

} // namespace vx
