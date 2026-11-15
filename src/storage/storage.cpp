#include "storage/storage.h"
#include "core/profiler.h"
#include <ctime>

namespace vx {

SaveFault WorldStorage::s_fault = SaveFault::None;

// ============================================================ chunk record codec
namespace {
struct Palette {
    std::vector<std::string> names;
    std::unordered_map<std::string, u32> idx;
    u32 add(const std::string& n) { auto it = idx.find(n); if (it != idx.end()) return it->second; u32 i = (u32)names.size(); names.push_back(n); idx[n] = i; return i; }
    void write(ByteWriter& w) const { w.var(names.size()); for (auto& n : names) w.str(n); }
};

void writeExtras(ByteWriter& w, const Chunk& c, const BlockRegistry& reg, const ItemNames& items, i64 worldTick, Palette& bp, Palette& ip) {
    // block entities
    w.var(c.blockEntities.size());
    for (auto& be : c.blockEntities) {
        w.u16_(be.index); w.u8_(be.kind);
        for (int i = 0; i < 4; i++) w.svar(be.data[i]);
        size_t ne = 0; for (auto& s : be.items) if (!s.empty()) ne++;
        w.var(be.items.size()); w.var(ne);
        for (size_t i = 0; i < be.items.size(); i++) {
            const ItemStack& s = be.items[i]; if (s.empty()) continue;
            w.var(i); w.var(ip.add(items.name ? items.name(s.item) : std::to_string(s.item))); w.u8_(s.count); w.var(s.damage);
        }
    }
    // scheduled ticks
    w.var(c.ticks.size());
    for (auto& t : c.ticks) { w.u16_(t.index); w.var(bp.add(reg.stateString(t.block))); w.svar(t.due - worldTick); w.u8_(t.priority); }
    // entities
    w.var(c.entities.size());
    for (auto& e : c.entities) { w.str(e.type); w.var(e.payload.size()); w.bytes(e.payload.data(), e.payload.size()); }
}

std::vector<u8> finishRecord(RecordKind kind, const Palette& bp, const Palette& ip, const ByteWriter& body, RecordStats* st) {
    ByteWriter raw;
    raw.u8_((u8)kind);
    bp.write(raw); ip.write(raw);
    raw.bytes(body.buf.data(), body.buf.size());
    ByteWriter out;
    std::vector<u8> comp;
    if (raw.size() > 96) comp = compressLZ4(raw.buf.data(), raw.size());
    bool useComp = !comp.empty() && comp.size() + 4 < raw.size();
    if (useComp) { out.u8_(1); out.var(raw.size()); out.bytes(comp.data(), comp.size()); }
    else { out.u8_(0); out.bytes(raw.buf.data(), raw.size()); }
    if (st) { st->rawBytes = raw.size(); st->storedBytes = out.size(); st->compressed = useComp; st->kind = kind; }
    return std::move(out.buf);
}
} // namespace

std::vector<u8> encodeChunkRecord(const Chunk& c, const BlockRegistry& reg, const ItemNames& items, i64 worldTick, RecordStats* st) {
    Palette bp, ip; ByteWriter body;
    std::vector<std::pair<u16, BlockState>> entries;
    entries.reserve(c.delta.size());
    for (auto& [idx, orig] : c.delta) { int lx, y, lz; columnIndexDecode(idx, lx, y, lz); entries.push_back({idx, c.get(lx, y, lz)}); }
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.first < b.first; });
    body.var(entries.size());
    u32 prev = 0;
    for (auto& [idx, s] : entries) { body.var(idx - prev); prev = idx; body.var(bp.add(reg.stateString(s))); }
    writeExtras(body, c, reg, items, worldTick, bp, ip);
    if (st) st->deltaCount = (int)entries.size();
    return finishRecord(RecordKind::Sparse, bp, ip, body, st);
}

std::vector<u8> encodeChunkRecordFull(const Chunk& c, const BlockRegistry& reg, const ItemNames& items, i64 worldTick) {
    Palette bp, ip; ByteWriter body;
    for (int s = 0; s < SECTIONS; s++) {
        const Section& sec = c.sec[s];
        if (sec.isUniform()) { body.u8_(0); body.var(bp.add(reg.stateString(sec.uniform))); }
        else { body.u8_(1); for (int i = 0; i < 4096; i++) body.var(bp.add(reg.stateString(sec.get(i)))); }
    }
    writeExtras(body, c, reg, items, worldTick, bp, ip);
    return finishRecord(RecordKind::Full, bp, ip, body, nullptr);
}

bool decodeChunkRecord(const u8* data, size_t n, Chunk& c, const BlockRegistry& reg, const ItemNames& items, i64 worldTick, RecordKind* kindOut, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (n < 1) return fail("empty record");
    std::vector<u8> rawBuf; const u8* raw; size_t rawN;
    if (data[0] == 1) {
        ByteReader h(data + 1, n - 1); u64 rawSize = h.var();
        if (!h.ok() || rawSize > (64u << 20)) return fail("bad record size");
        if (!decompressLZ4(data + 1 + h.pos, n - 1 - h.pos, (size_t)rawSize, rawBuf)) return fail("lz4 decode failed");
        raw = rawBuf.data(); rawN = rawBuf.size();
    } else if (data[0] == 0) { raw = data + 1; rawN = n - 1; }
    else return fail("unknown record encoding");
    ByteReader r(raw, rawN);
    RecordKind kind = (RecordKind)r.u8_();
    if (kindOut) *kindOut = kind;
    std::vector<BlockState> bpal; std::vector<u16> ipal;
    u64 nb = r.var(); if (nb > 65536) return fail("bad block palette");
    for (u64 i = 0; i < nb && r.ok(); i++) {
        std::string s = r.str(); BlockState st = reg.parseState(s);
        if (st == AIR && s.rfind("air", 0) != 0) VX_WARN("unknown block '%s' in saved chunk %d,%d (replaced by air)", s.c_str(), c.pos.x, c.pos.z);
        bpal.push_back(st);
    }
    u64 ni = r.var(); if (ni > 65536) return fail("bad item palette");
    for (u64 i = 0; i < ni && r.ok(); i++) { std::string s = r.str(); ipal.push_back(items.id ? items.id(s) : (u16)atoi(s.c_str())); }
    auto bstate = [&](u64 i) -> BlockState { return i < bpal.size() ? bpal[(size_t)i] : AIR; };
    if (kind == RecordKind::Sparse) {
        u64 cnt = r.var(); if (cnt > 65536) return fail("bad delta count");
        u32 idx = 0;
        for (u64 i = 0; i < cnt && r.ok(); i++) {
            idx += (u32)r.var(); if (idx > 0xFFFF) return fail("bad delta index");
            c.applyDelta((u16)idx, bstate(r.var()));
        }
    } else if (kind == RecordKind::Full) {
        for (int s = 0; s < SECTIONS && r.ok(); s++) {
            u8 mode = r.u8_();
            if (mode == 0) { BlockState st = bstate(r.var()); for (int i = 0; i < 4096; i++) { int y = (s << 4) | (i >> 8); c.applyDelta(columnIndex(i & 15, y, (i >> 4) & 15), st); } }
            else for (int i = 0; i < 4096 && r.ok(); i++) { int y = (s << 4) | (i >> 8); c.applyDelta(columnIndex(i & 15, y, (i >> 4) & 15), bstate(r.var())); }
        }
    } else return fail("unknown record kind");
    // block entities
    u64 nbe = r.var(); if (nbe > 65536) return fail("bad block entity count");
    c.blockEntities.clear();
    for (u64 i = 0; i < nbe && r.ok(); i++) {
        BlockEntity be; be.index = r.u16_(); be.kind = r.u8_();
        for (int k = 0; k < 4; k++) be.data[k] = (i32)r.svar();
        u64 slots = r.var(), ne = r.var(); if (slots > 1024 || ne > slots) return fail("bad container");
        be.items.resize((size_t)slots);
        for (u64 k = 0; k < ne && r.ok(); k++) {
            u64 slot = r.var(); u64 ii = r.var(); ItemStack s; s.count = r.u8_(); s.damage = (u16)r.var();
            s.item = ii < ipal.size() ? ipal[(size_t)ii] : 0;
            if (slot < slots) be.items[(size_t)slot] = s;
        }
        c.blockEntities.push_back(std::move(be));
    }
    u64 nt = r.var(); if (nt > 1u << 20) return fail("bad tick count");
    c.ticks.clear();
    for (u64 i = 0; i < nt && r.ok(); i++) {
        ScheduledTick t; t.index = r.u16_(); t.block = bstate(r.var()); t.due = worldTick + r.svar(); t.priority = r.u8_();
        c.ticks.push_back(t);
    }
    u64 ne = r.var(); if (ne > 1u << 16) return fail("bad entity count");
    c.entities.clear();
    for (u64 i = 0; i < ne && r.ok(); i++) {
        EntityRecord e; e.type = r.str(); u64 len = r.var();
        const u8* p = r.skip((size_t)len); if (!p) break;
        e.payload.assign(p, p + len); c.entities.push_back(std::move(e));
    }
    if (!r.ok()) return fail("truncated record");
    return true;
}

// ============================================================ region file
std::vector<u8> RegionFile::serialize() const {
    ByteWriter w;
    w.u32_(REGION_MAGIC); w.u16_(REGION_FORMAT); w.u16_(0); w.i32_(pos.x); w.i32_(pos.z);
    w.var(records.size());
    for (auto& [local, rec] : records) { w.u16_(local); w.var(rec.size()); w.bytes(rec.data(), rec.size()); w.u32_(crc32(rec.data(), rec.size())); }
    w.u32_(crc32(w.buf.data(), w.buf.size()));
    return std::move(w.buf);
}

bool RegionFile::parse(const std::vector<u8>& bytes, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (bytes.size() < 20) return fail("region too small");
    u32 stored = (u32)bytes[bytes.size() - 4] | ((u32)bytes[bytes.size() - 3] << 8) | ((u32)bytes[bytes.size() - 2] << 16) | ((u32)bytes[bytes.size() - 1] << 24);
    if (crc32(bytes.data(), bytes.size() - 4) != stored) return fail("region checksum mismatch");
    ByteReader r(bytes.data(), bytes.size() - 4);
    if (r.u32_() != REGION_MAGIC) return fail("bad region magic");
    u16 fmt = r.u16_(); r.u16_();
    if (fmt != REGION_FORMAT) return fail("unsupported region format");
    pos.x = r.i32_(); pos.z = r.i32_();
    u64 n = r.var(); if (n > 1024) return fail("bad record count");
    records.clear();
    for (u64 i = 0; i < n && r.ok(); i++) {
        u16 local = r.u16_(); u64 len = r.var();
        const u8* p = r.skip((size_t)len); if (!p) break;
        u32 crc = r.u32_();
        if (crc32(p, (size_t)len) != crc) return fail("record checksum mismatch");
        records[local].assign(p, p + len);
    }
    if (!r.ok()) return fail("truncated region");
    return true;
}

// ============================================================ meta
Json WorldMeta::toJson() const {
    Json j = Json::makeObject();
    j.set("format", Json::of((double)formatVersion));
    j.set("name", Json::of(name));
    Json g = Json::makeObject();
    g.set("seed", Json::of(std::to_string(gen.seed)));   // string: u64 does not fit a double
    g.set("version", Json::of((double)gen.version));
    g.set("dimension", Json::of(gen.dimension));
    g.set("preset", Json::of(gen.preset));
    g.set("terrain_scale", Json::of((double)gen.terrainScale));
    g.set("caves", Json::of(gen.caves)); g.set("ores", Json::of(gen.ores)); g.set("trees", Json::of(gen.trees)); g.set("structures", Json::of(gen.structures));
    j.set("generator", g);
    j.set("tick", Json::of(std::to_string(tick)));
    j.set("day_time", Json::of((double)dayTime));
    Json sp = Json::makeArray(); sp.push(Json::of((double)spawn.x)); sp.push(Json::of((double)spawn.y)); sp.push(Json::of((double)spawn.z));
    j.set("spawn", sp);
    j.set("game_mode", Json::of(gameMode));
    j.set("raining", Json::of(raining)); j.set("weather_ticks", Json::of((double)weatherTicks));
    Json pv = Json::makeArray();
    for (auto& [a, b] : populatedVillages) { Json e = Json::makeArray(); e.push(Json::of((double)a)); e.push(Json::of((double)b)); pv.push(e); }
    j.set("populated_villages", pv);
    j.set("created", Json::of((double)createdUnix));
    j.set("last_played", Json::of((double)lastPlayedUnix));
    return j;
}

bool WorldMeta::fromJson(const Json& j, WorldMeta& m, std::string* err) {
    if (!j.isObject()) { if (err) *err = "world.json is not an object"; return false; }
    m.formatVersion = (u32)j["format"].asInt(1);
    if (m.formatVersion != 1) { if (err) *err = "unsupported world format " + std::to_string(m.formatVersion); return false; }
    m.name = j["name"].asString("World");
    const Json& g = j["generator"];
    m.gen.seed = std::strtoull(g["seed"].asString("0").c_str(), nullptr, 10);
    m.gen.version = (u32)g["version"].asInt(1);
    m.gen.dimension = g["dimension"].asString("overworld");
    m.gen.preset = g["preset"].asString("default");
    m.gen.terrainScale = (float)g["terrain_scale"].asNumber(1.0);
    m.gen.caves = g["caves"].asBool(true); m.gen.ores = g["ores"].asBool(true); m.gen.trees = g["trees"].asBool(true); m.gen.structures = g["structures"].asBool(true);
    m.tick = std::strtoll(j["tick"].asString("0").c_str(), nullptr, 10);
    m.dayTime = (i64)j["day_time"].asNumber(1000);
    const Json& sp = j["spawn"];
    if (sp.isArray() && sp.size() == 3) m.spawn = {sp[0].asInt(), sp[1].asInt(), sp[2].asInt()};
    m.gameMode = j["game_mode"].asString("survival");
    m.raining = j["raining"].asBool(false); m.weatherTicks = (i64)j["weather_ticks"].asNumber(12000);
    m.populatedVillages.clear();
    for (auto& e : j["populated_villages"].arr) m.populatedVillages.push_back({e[0].asInt(), e[1].asInt()});
    m.createdUnix = (u64)j["created"].asNumber(0);
    m.lastPlayedUnix = (u64)j["last_played"].asNumber(0);
    return true;
}

// ============================================================ storage
fs::path WorldStorage::regionPath(RegionPos p) const {
    return dir_ / "region" / ("r." + std::to_string(p.x) + "." + std::to_string(p.z) + ".vxr");
}

bool WorldStorage::open(const fs::path& dir, bool create, std::string* err) {
    std::lock_guard<std::mutex> lk(m_);
    dir_ = dir; cache_.clear(); missing_.clear(); lastUse_.clear();
    std::error_code ec;
    if (!fs::exists(dir_ / "world.json", ec) && !fs::exists(dir_ / "world.json.new", ec)) {
        if (!create) { if (err) *err = "not a world: " + dir_.string(); return false; }
    }
    fs::create_directories(dir_ / "region", ec);
    if (ec) { if (err) *err = "cannot create world directory: " + ec.message(); return false; }
    return recover(err);
}

static const u32 JOURNAL_MAGIC = 0x4E4A5856;   // "VXJN"

bool WorldStorage::recover(std::string* err) {
    lastRecovery.clear();
    std::error_code ec;
    fs::path jpath = dir_ / "save.journal";
    int rolled = 0;
    if (fs::exists(jpath, ec)) {
        auto bytes = readFile(jpath);
        bool valid = false;
        std::vector<std::pair<u8, std::string>> ops;
        if (bytes && bytes->size() >= 12) {
            u32 stored = (u32)(*bytes)[bytes->size() - 4] | ((u32)(*bytes)[bytes->size() - 3] << 8) | ((u32)(*bytes)[bytes->size() - 2] << 16) | ((u32)(*bytes)[bytes->size() - 1] << 24);
            if (crc32(bytes->data(), bytes->size() - 4) == stored) {
                ByteReader r(bytes->data(), bytes->size() - 4);
                if (r.u32_() == JOURNAL_MAGIC) {
                    u64 n = r.var();
                    for (u64 i = 0; i < n && r.ok(); i++) { u8 op = r.u8_(); ops.push_back({op, r.str()}); }
                    valid = r.ok();
                }
            }
        }
        if (valid) {
            // Roll forward: the journal is only written after every new file is durable.
            for (auto& [op, rel] : ops) {
                fs::path fin = dir_ / pathFromUtf8(rel);
                fs::path nw = fin; nw += ".new";
                if (op == 1) { if (fs::exists(nw, ec)) { if (!renameReplace(nw, fin)) { if (err) *err = "recovery rename failed: " + rel; return false; } rolled++; } }
                else if (op == 2) { fs::remove(fin, ec); rolled++; }
            }
            syncDirectory(dir_ / "region"); syncDirectory(dir_);
            lastRecovery = "completed interrupted save (" + std::to_string(rolled) + " operations)";
        } else {
            fs::path bad = jpath; bad += ".corrupt";
            renameReplace(jpath, bad);
            lastRecovery = "discarded corrupt save journal; previous save kept";
        }
        fs::remove(jpath, ec);
    }
    // Anything still named *.new / *.tmp belongs to a save that never reached its journal: roll back.
    int removed = 0;
    for (const fs::path& d : {dir_, dir_ / "region"}) {
        for (auto& e : fs::directory_iterator(d, ec)) {
            auto ext = e.path().extension().string();
            if (e.is_regular_file(ec) && (ext == ".new" || ext == ".tmp")) { fs::remove(e.path(), ec); removed++; }
        }
    }
    if (removed) { if (!lastRecovery.empty()) lastRecovery += "; "; lastRecovery += "discarded " + std::to_string(removed) + " incomplete file(s) from an interrupted save"; }
    if (!lastRecovery.empty()) VX_WARN("world recovery: %s", lastRecovery.c_str());
    return true;
}

bool WorldStorage::loadMeta(WorldMeta& out, std::string* err) {
    auto txt = readTextFile(dir_ / "world.json");
    if (!txt) { if (err) *err = "missing world.json"; return false; }
    std::string perr; Json j = Json::parse(*txt, &perr);
    if (!perr.empty()) { if (err) *err = "world.json: " + perr; return false; }
    return WorldMeta::fromJson(j, out, err);
}

RegionFile* WorldStorage::cachedRegion(RegionPos p) {
    lastUse_[p] = ++useClock_;
    auto it = cache_.find(p);
    if (it != cache_.end()) return &it->second;
    if (missing_.count(p)) return nullptr;
    auto bytes = readFile(regionPath(p));
    if (!bytes) { missing_[p] = true; return nullptr; }
    RegionFile rf; std::string err;
    if (!rf.parse(*bytes, &err)) {
        // Never silently drop: keep the damaged file aside for manual recovery and continue.
        fs::path bad = regionPath(p); bad += ".corrupt";
        std::error_code ec; fs::copy_file(regionPath(p), bad, fs::copy_options::overwrite_existing, ec);
        VX_ERROR("region %d,%d unreadable (%s); copy kept at %s", p.x, p.z, err.c_str(), bad.string().c_str());
        missing_[p] = true;
        return nullptr;
    }
    rf.pos = p;
    return &(cache_[p] = std::move(rf));
}

void WorldStorage::trimCache() {
    // The cache always mirrors what is on disk after a commit, so evicting is just forgetting.
    while (cache_.size() > maxCachedRegions) {
        auto victim = cache_.begin(); u64 best = UINT64_MAX;
        for (auto it = cache_.begin(); it != cache_.end(); ++it) { u64 u = lastUse_[it->first]; if (u < best) { best = u; victim = it; } }
        lastUse_.erase(victim->first);
        cache_.erase(victim);
    }
    if (missing_.size() > 4096) missing_.clear();   // negative cache: cheap to rebuild
}

std::vector<u8> WorldStorage::readRecord(ChunkPos c) {
    std::lock_guard<std::mutex> lk(m_);
    std::vector<u8> out;
    if (RegionFile* rf = cachedRegion(regionOf(c))) {
        auto it = rf->records.find(regionLocal(c));
        if (it != rf->records.end()) out = it->second;
    }
    trimCache();
    return out;
}

bool WorldStorage::hasRecord(ChunkPos c) {
    std::lock_guard<std::mutex> lk(m_);
    RegionFile* rf = cachedRegion(regionOf(c));
    bool has = rf && rf->records.count(regionLocal(c));
    trimCache();
    return has;
}

std::optional<std::vector<u8>> WorldStorage::readExtra(const std::string& name) { return readFile(dir_ / pathFromUtf8(name)); }

bool WorldStorage::commit(const Batch& b, std::string* err) {
    VX_TIME_US(Stat::SaveTimeUs);
    std::lock_guard<std::mutex> lk(m_);
    std::error_code ec;
    std::map<RegionPos, bool> touched;
    for (auto& [cp, rec] : b.chunks) {
        RegionPos rp = regionOf(cp);
        RegionFile* rf = cachedRegion(rp);
        if (!rf) {
            if (rec.empty()) continue;   // deleting a record that was never stored
            RegionFile nr; nr.pos = rp; missing_.erase(rp); rf = &(cache_[rp] = std::move(nr));
        }
        if (rec.empty()) rf->records.erase(regionLocal(cp)); else rf->records[regionLocal(cp)] = rec;
        touched[rp] = true;
    }
    // 1. write every new file durably under "<name>.new"
    std::vector<std::pair<u8, std::string>> ops;
    for (auto& [rp, _] : touched) {
        RegionFile& rf = cache_[rp];
        std::string rel = "region/" + regionPath(rp).filename().string();
        if (rf.records.empty()) {
            if (fs::exists(regionPath(rp), ec)) ops.push_back({2, rel});
            cache_.erase(rp); missing_[rp] = true;
            continue;
        }
        auto bytes = rf.serialize();
        fs::path nw = regionPath(rp); nw += ".new";
        if (!writeFileDurable(nw, bytes.data(), bytes.size())) { if (err) *err = "write failed: " + nw.string(); return false; }
        ops.push_back({1, rel});
    }
    if (b.meta) {
        std::string txt = b.meta->toJson().dump(2);
        if (!writeFileDurable(dir_ / "world.json.new", txt.data(), txt.size())) { if (err) *err = "write failed: world.json"; return false; }
        ops.push_back({1, "world.json"});
    }
    for (auto& [name, bytes] : b.extraFiles) {
        fs::path nw = dir_ / pathFromUtf8(name); nw += ".new";
        if (!writeFileDurable(nw, bytes.data(), bytes.size())) { if (err) *err = "write failed: " + name; return false; }
        ops.push_back({1, name});
    }
    syncDirectory(dir_ / "region"); syncDirectory(dir_);
    if (ops.empty()) { trimCache(); return true; }
    if (s_fault == SaveFault::AfterNewFiles) { if (err) *err = "simulated crash after new files"; return false; }
    // 2. commit point: journal written atomically (tmp + rename)
    ByteWriter jw; jw.u32_(JOURNAL_MAGIC); jw.var(ops.size());
    for (auto& [op, rel] : ops) { jw.u8_(op); jw.str(rel); }
    jw.u32_(crc32(jw.buf.data(), jw.buf.size()));
    if (!writeFileAtomic(dir_ / "save.journal", jw.buf)) { if (err) *err = "journal write failed"; return false; }
    if (s_fault == SaveFault::AfterJournal) { if (err) *err = "simulated crash after journal"; return false; }
    // 3. apply
    int applied = 0;
    for (auto& [op, rel] : ops) {
        fs::path fin = dir_ / pathFromUtf8(rel);
        if (op == 1) { fs::path nw = fin; nw += ".new"; if (!renameReplace(nw, fin)) { if (err) *err = "rename failed: " + rel; return false; } }
        else fs::remove(fin, ec);
        if (++applied == 1 && s_fault == SaveFault::MidRename) { if (err) *err = "simulated crash mid-rename"; return false; }
    }
    syncDirectory(dir_ / "region"); syncDirectory(dir_);
    fs::remove(dir_ / "save.journal", ec);
    trimCache();   // only after a successful commit: the cache then mirrors the disk exactly
    return true;
}

std::vector<RegionPos> WorldStorage::listRegions() {
    std::vector<RegionPos> out; std::error_code ec;
    for (auto& e : fs::directory_iterator(dir_ / "region", ec)) {
        std::string n = e.path().filename().string();
        int x, z; char tail[8] = {0};
        if (sscanf(n.c_str(), "r.%d.%d.%4s", &x, &z, tail) == 3 && std::string(tail) == "vxr") out.push_back({x, z});
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool WorldStorage::loadRegion(RegionPos rp, RegionFile& out, std::string* err) {
    std::lock_guard<std::mutex> lk(m_);
    RegionFile* rf = cachedRegion(rp);
    if (!rf) { if (err) *err = "no region"; return false; }
    out = *rf; return true;
}

WorldStorage::Usage WorldStorage::usage() {
    Usage u; std::error_code ec;
    for (RegionPos rp : listRegions()) {
        u.regionFiles++; u.regionBytes += fs::file_size(regionPath(rp), ec);
        std::lock_guard<std::mutex> lk(m_);
        if (RegionFile* rf = cachedRegion(rp)) u.records += rf->records.size();
    }
    u.totalBytes = directorySize(dir_);
    return u;
}

} // namespace vx
