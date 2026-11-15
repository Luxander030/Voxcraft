// Critical storage tests (CLAUDE.md 24.1, Tests A-F) plus streaming/corruption/migration cases.
#include "testing.h"
#include "world/world.h"
#include "storage/maintenance.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(4); return j; }

WorldMeta testMeta(u64 seed) { WorldMeta m; m.name = "test"; m.gen.seed = seed; m.spawn = {8, 100, 8}; return m; }

u64 chunkHash(const Chunk& c) {
    u64 h = 0xcbf29ce484222325ull;
    for (int y = 0; y < WORLD_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) { BlockState s = c.get(x, y, z); h = fnv1a(&s, 2, h); }
    return h;
}
size_t regionFileCount(const fs::path& dir) {
    size_t n = 0; std::error_code ec;
    for (auto& e : fs::directory_iterator(dir / "region", ec)) if (e.path().extension() == ".vxr") n++;
    return n;
}
// a position that is air directly above a solid block (any column, any height)
BlockPos airAbove(const Chunk& c) {
    for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) for (int y = WORLD_H - 2; y > 0; y--) {
        BlockState s = c.get(x, y, z);
        if (blocks().solid(s) && c.get(x, y + 1, z) == AIR) return {c.pos.x * 16 + x, y + 1, c.pos.z * 16 + z};
    }
    return {c.pos.x * 16, 200, c.pos.z * 16};
}
// a solid, breakable generated block
BlockPos solidBlock(const Chunk& c) {
    for (int y = 40; y < WORLD_H; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        BlockState s = c.get(x, y, z);
        if (blocks().solid(s) && blocks().def(s).hardness >= 0) return {c.pos.x * 16 + x, y, c.pos.z * 16 + z};
    }
    return {c.pos.x * 16, 1, c.pos.z * 16};
}
} // namespace

TEST(storage_A_unmodified_chunk_not_persisted) {
    auto dir = vxtest::tempDir("A");
    u64 h1;
    {
        World w(blocks(), jobs(), vxtest::testItems());
        std::string err; REQUIRE(w.create(dir, testMeta(42), &err));
        ChunkPtr c = w.loadChunkSync({3, -4});
        h1 = chunkHash(*c);
        CHECK(!c->hasPersistentData());
        REQUIRE(w.save(true, {}, &err));
        CHECK(!w.storage().hasRecord({3, -4}));
        CHECK_EQ(regionFileCount(dir), (size_t)0);
        w.close();
    }
    World w(blocks(), jobs(), vxtest::testItems());
    std::string err; REQUIRE(w.open(dir, &err));
    ChunkPtr c = w.loadChunkSync({3, -4});
    CHECK_EQ(chunkHash(*c), h1);
}

TEST(storage_B_modified_chunk_persists) {
    auto dir = vxtest::tempDir("B");
    BlockPos p; BlockState stone = blocks().stateOf("stone_bricks");
    {
        World w(blocks(), jobs(), vxtest::testItems());
        std::string err; REQUIRE(w.create(dir, testMeta(7), &err));
        ChunkPtr c = w.loadChunkSync({0, 0});
        p = airAbove(*c);
        REQUIRE(w.setBlock(p, stone));
        CHECK(c->hasPersistentData());
        CHECK_EQ(c->delta.size(), (size_t)1);
        REQUIRE(w.save(true, {}, &err));
        CHECK(w.storage().hasRecord({0, 0}));
    }
    World w(blocks(), jobs(), vxtest::testItems());
    std::string err; REQUIRE(w.open(dir, &err));
    w.loadChunkSync({0, 0});
    CHECK_EQ(w.getBlock(p), stone);
    // a modified chunk record is tiny: one delta entry + palette
    auto rec = w.storage().readRecord({0, 0});
    CHECK(rec.size() < 64);
}

TEST(storage_C_reverted_chunk_record_removed) {
    auto dir = vxtest::tempDir("C");
    World w(blocks(), jobs(), vxtest::testItems());
    std::string err; REQUIRE(w.create(dir, testMeta(9), &err));
    ChunkPtr c = w.loadChunkSync({1, 1});
    BlockPos p = airAbove(*c);
    BlockPos below = solidBlock(*c);
    BlockState orig = w.getBlock(below);
    REQUIRE(orig != AIR);
    // place + remove in memory: never needs storage
    REQUIRE(w.setBlock(p, blocks().stateOf("glass")));
    REQUIRE(w.setBlock(p, AIR));
    CHECK(!c->hasPersistentData());
    // persisted change, later reverted: record must be removed
    REQUIRE(w.setBlock(below, AIR));
    REQUIRE(w.save(true, {}, &err));
    CHECK(w.storage().hasRecord({1, 1}));
    CHECK_EQ(regionFileCount(dir), (size_t)1);
    REQUIRE(w.setBlock(below, orig));
    CHECK(c->delta.empty());
    CHECK(!c->hasPersistentData());
    REQUIRE(w.save(true, {}, &err));
    CHECK(!w.storage().hasRecord({1, 1}));
    CHECK_EQ(regionFileCount(dir), (size_t)0);   // empty region file deleted too
}

TEST(storage_D_container_contents_persist) {
    auto dir = vxtest::tempDir("D");
    BlockPos p;
    {
        World w(blocks(), jobs(), vxtest::testItems());
        std::string err; REQUIRE(w.create(dir, testMeta(11), &err));
        ChunkPtr c = w.loadChunkSync({-2, 5});
        p = airAbove(*c);
        REQUIRE(w.setBlock(p, blocks().stateOf("chest")));
        BlockEntity* be = w.ensureBlockEntity(p, BE_CHEST);
        REQUIRE(be);
        CHECK_EQ(be->items.size(), (size_t)27);
        be->items[0] = {5, 64, 0};
        be->items[13] = {9, 3, 17};
        be->items[26] = {200, 1, 250};
        REQUIRE(w.save(true, {}, &err));
    }
    World w(blocks(), jobs(), vxtest::testItems());
    std::string err; REQUIRE(w.open(dir, &err));
    w.loadChunkSync({-2, 5});
    CHECK_EQ(blocks().def(w.getBlock(p)).name, std::string("chest"));
    BlockEntity* be = w.blockEntity(p);
    REQUIRE(be);
    CHECK_EQ(be->items.size(), (size_t)27);
    CHECK(be->items[0] == (ItemStack{5, 64, 0}));
    CHECK(be->items[13] == (ItemStack{9, 3, 17}));
    CHECK(be->items[26] == (ItemStack{200, 1, 250}));
    CHECK(be->items[1].empty());
}

// Test E (cross-platform): the format is explicitly little-endian with fixed-width/varint fields and
// generation avoids non-portable float ops. These golden values must match on every platform/arch;
// CI runs this on Windows x64 and macOS arm64. If generation changes intentionally, bump the
// generator version instead of editing the hashes.
TEST(storage_E_cross_platform_golden) {
    World w(blocks(), jobs(), vxtest::testItems());
    auto dir = vxtest::tempDir("E");
    std::string err; REQUIRE(w.create(dir, testMeta(123456789), &err));
    u64 h = 0;
    for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) h ^= chunkHash(*w.loadChunkSync({x * 37, z * 41})) * (u64)(x + 7 + (z + 5) * 13);
    printf("    generation golden hash: %016llx\n", (unsigned long long)h);
    CHECK_EQ(h, (u64)0x0d786fd5fbccdd30ull);   // pinned on macOS arm64; CI checks Windows x64 / macOS x64 match.
    // A change here means existing worlds would regenerate differently: bump LATEST_GENERATOR_VERSION instead.
    // record bytes are platform-independent: encode a known chunk modification
    ChunkPtr c = w.loadChunkSync({0, 0});
    w.setBlock({1, 200, 2}, blocks().stateOf("stone"));
    w.setBlock({3, 201, 4}, blocks().stateOf("oak_planks"));
    auto rec = encodeChunkRecord(*c, blocks(), vxtest::testItems(), 0);
    printf("    record crc: %08x (%zu bytes)\n", crc32(rec.data(), rec.size()), rec.size());
    CHECK_EQ(crc32(rec.data(), rec.size()), (u32)0x123e3415u);   // record bytes are part of the on-disk format
}

TEST(storage_F_crash_recovery) {
    for (SaveFault fault : {SaveFault::AfterNewFiles, SaveFault::AfterJournal, SaveFault::MidRename}) {
        auto dir = vxtest::tempDir("F" + std::to_string((int)fault));
        BlockPos p1, p2; BlockState a = blocks().stateOf("bricks"), b = blocks().stateOf("wool");
        {
            World w(blocks(), jobs(), vxtest::testItems());
            std::string err; REQUIRE(w.create(dir, testMeta(5), &err));
            ChunkPtr c1 = w.loadChunkSync({0, 0});
            ChunkPtr c2 = w.loadChunkSync({40, 40});     // different region file
            p1 = airAbove(*c1); p2 = airAbove(*c2);
            REQUIRE(w.setBlock(p1, a));
            REQUIRE(w.save(true, {}, &err));              // valid save #1: p1 = bricks
            REQUIRE(w.setBlock(p2, b));
            REQUIRE(w.setBlock(p1, b));
            WorldStorage::s_fault = fault;
            CHECK(!w.save(true, {}, &err));               // interrupted save #2
            WorldStorage::s_fault = SaveFault::None;
            // simulate the process dying at this instant: snapshot the directory now. (The World
            // destructor below then saves normally into `dir`, which the test no longer uses.)
            std::error_code ec;
            fs::copy(dir, fs::path(dir.string() + "_crash"), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
        fs::path crash = fs::path(dir.string() + "_crash");
        {
        World w(blocks(), jobs(), vxtest::testItems());
        std::string err; REQUIRE(w.open(crash, &err));
        CHECK(!w.storage().lastRecovery.empty());
        w.loadChunkSync({0, 0}); w.loadChunkSync({40, 40});
        if (fault == SaveFault::AfterNewFiles) {
            CHECK_EQ(w.getBlock(p1), a);          // previous valid save intact
            CHECK(w.getBlock(p2) != b);
        } else {
            CHECK_EQ(w.getBlock(p1), b);          // journal reached: save completed on recovery
            CHECK_EQ(w.getBlock(p2), b);
        }
        std::error_code ec;
        CHECK(!fs::exists(crash / "save.journal", ec));
        for (auto& e : fs::directory_iterator(crash / "region", ec)) CHECK(e.path().extension() != ".new");
        }
        std::error_code ec2; fs::remove_all(crash, ec2);
    }
}

TEST(storage_streaming_unload_reload) {
    auto dir = vxtest::tempDir("stream");
    World w(blocks(), jobs(), vxtest::testItems());
    w.budget.chunkCacheBytes = 0;   // force regeneration on return
    std::string err; REQUIRE(w.create(dir, testMeta(77), &err));
    w.setFocus({8, 80, 8}, 3);
    w.waitForJobs();
    REQUIRE(w.isReady({0, 0}));
    ChunkPtr c = w.chunk({0, 0});
    BlockPos p = airAbove(*c);
    REQUIRE(w.setBlock(p, blocks().stateOf("glowcrystal")));
    // walk away: chunk unloads, its record goes to the pending-write set
    w.setFocus({16 * 40, 80, 0}, 3);
    w.waitForJobs();
    CHECK(!w.chunk({0, 0}));
    CHECK(w.stats().pendingWrites >= 1);
    // come back before any save: must be loaded from the pending record
    w.setFocus({8, 80, 8}, 3);
    w.waitForJobs();
    REQUIRE(w.chunk({0, 0}));
    CHECK_EQ(w.getBlock(p), blocks().stateOf("glowcrystal"));
    REQUIRE(w.save(true, {}, &err));
    CHECK_EQ(w.stats().pendingWrites, 0);
    // only the single modified chunk is stored, despite ~100 chunks having been loaded
    auto u = w.storage().usage();
    CHECK_EQ(u.records, (u64)1);
}

TEST(storage_light_is_ready_after_streaming) {
    auto dir = vxtest::tempDir("light");
    World w(blocks(), jobs(), vxtest::testItems());
    std::string err; REQUIRE(w.create(dir, testMeta(3), &err));
    w.setFocus({0, 80, 0}, 2);
    w.waitForJobs();
    REQUIRE(w.isReady({0, 0}));
    ChunkPtr c = w.chunk({0, 0});
    // top of the world is fully sky-lit
    CHECK_EQ((int)c->skyLight(5, 250, 5), 15);
    // deep underground (below the highest light-blocking block) is dark unless in a lit cave
    int hm = c->heightmap[5 + 5 * 16];
    CHECK(hm > 0);
    CHECK_EQ((int)c->skyLight(5, 1, 5), 0);
}

TEST(storage_corrupt_region_is_preserved) {
    auto dir = vxtest::tempDir("corrupt");
    BlockPos p;
    {
        World w(blocks(), jobs(), vxtest::testItems());
        std::string err; REQUIRE(w.create(dir, testMeta(2), &err));
        ChunkPtr c = w.loadChunkSync({0, 0}); p = airAbove(*c);
        w.setBlock(p, blocks().stateOf("bricks"));
        REQUIRE(w.save(true, {}, &err));
    }
    fs::path rf = dir / "region" / "r.0.0.vxr";
    auto bytes = *readFile(rf);
    bytes[bytes.size() / 2] ^= 0x5A;
    writeFileAtomic(rf, bytes);
    World w(blocks(), jobs(), vxtest::testItems());
    std::string err; REQUIRE(w.open(dir, &err));
    w.loadChunkSync({0, 0});
    std::error_code ec;
    CHECK(fs::exists(dir / "region" / "r.0.0.vxr.corrupt", ec));   // damaged data kept for recovery
}

TEST(storage_migration_full_to_sparse) {
    auto dir = vxtest::tempDir("migrate");
    BlockPos p;
    std::vector<u8> fullC, fullU;
    {
        World w(blocks(), jobs(), vxtest::testItems());
        std::string err; REQUIRE(w.create(dir, testMeta(31), &err));
        ChunkPtr c = w.loadChunkSync({2, 2}); p = airAbove(*c);
        w.setBlock(p, blocks().stateOf("bricks"));
        ChunkPtr u = w.loadChunkSync({3, 2});   // unmodified
        fullC = encodeChunkRecordFull(*c, blocks(), w.items, 0);
        fullU = encodeChunkRecordFull(*u, blocks(), w.items, 0);
    }
    {   // overwrite with legacy FULL records, as an older version would have written
        WorldStorage st; std::string err; REQUIRE(st.open(dir, false, &err));
        WorldStorage::Batch b;
        b.chunks.push_back({{2, 2}, fullC});
        b.chunks.push_back({{3, 2}, fullU});
        REQUIRE(st.commit(b, &err));
        CHECK(st.usage().regionBytes > 1000);
    }
    MaintenanceReport rep;
    std::string err;
    // dry run changes nothing
    REQUIRE(optimizeWorld(dir, blocks(), vxtest::testItems(), true, rep, &err));
    CHECK_EQ(rep.recordsRemoved, 1);
    CHECK_EQ(rep.recordsMigrated, 1);
    CHECK(fs::exists(dir / "region" / "r.0.0.vxr"));
    MaintenanceReport rep2;
    REQUIRE(optimizeWorld(dir, blocks(), vxtest::testItems(), false, rep2, &err));
    CHECK(rep2.bytesAfter < rep2.bytesBefore);
    CHECK(!rep2.backupDir.empty());
    std::error_code ec;
    CHECK(fs::exists(rep2.backupDir, ec));
    World w(blocks(), jobs(), vxtest::testItems());
    REQUIRE(w.open(dir, &err));
    w.loadChunkSync({2, 2});
    CHECK_EQ(w.getBlock(p), blocks().stateOf("bricks"));
    CHECK(!w.storage().hasRecord({3, 2}));
}

TEST(worldgen_v2_adds_structures_without_changing_v1_or_storage) {
    GenSettings v1; v1.seed = 2024; v1.version = 1;
    GenSettings v2 = v1; v2.version = 2;
    auto g1 = createGenerator(v1, blocks()), g2 = createGenerator(v2, blocks());
    REQUIRE(g1 && g2);
    CHECK(!createGenerator([] { GenSettings s; s.version = LATEST_GENERATOR_VERSION + 1; return s; }(), blocks()));   // unknown future version refused
    int differing = 0, chestChunks = 0, same = 0;
    BlockState chest = blocks().stateOf("chest");
    for (int cz = -12; cz < 12; cz++) for (int cx = -12; cx < 12; cx++) {
        Chunk a({cx, cz}), b({cx, cz});
        g1->generate(a); g2->generate(b);
        bool diff = false, hasChest = false;
        for (int s = 0; s < SECTIONS && !diff; s++) for (int i = 0; i < 4096; i++) if (a.sec[s].get(i) != b.sec[s].get(i)) { diff = true; break; }
        for (int s = 0; s < SECTIONS; s++) for (int i = 0; i < 4096; i++) if (blocks().def(b.sec[s].get(i)).index == blocks().def(chest).index) hasChest = true;
        differing += diff; same += !diff; chestChunks += hasChest;
        CHECK(!b.hasPersistentData());   // generated chests are empty: nothing to store
    }
    printf("    v2 vs v1 over 576 chunks: %d differ, %d identical, %d contain structure chests\n", differing, same, chestChunks);
    CHECK(differing > 0);          // v2 really adds structures
    CHECK(same > differing * 5);   // ...and leaves the rest of the terrain untouched
    // determinism of v2 itself
    Chunk x({3, 3}), y({3, 3}); g2->generate(x); g2->generate(y);
    for (int s = 0; s < SECTIONS; s++) for (int i = 0; i < 4096; i++) if (x.sec[s].get(i) != y.sec[s].get(i)) { CHECK(false); s = SECTIONS; break; }
}

TEST(world_meta_round_trips_weather_and_generator_fields) {
    WorldMeta m; m.name = "w"; m.gen.seed = 18446744073709551000ull; m.gen.version = 2; m.gen.dimension = "hollow";
    m.raining = true; m.weatherTicks = 4321; m.tick = 9876543210ll; m.gameMode = "creative";
    WorldMeta r; std::string err;
    REQUIRE(WorldMeta::fromJson(Json::parse(m.toJson().dump(), &err), r, &err));
    CHECK(r.raining); CHECK_EQ(r.weatherTicks, (i64)4321);
    CHECK(r.gen.seed == m.gen.seed); CHECK_EQ(r.gen.version, 2u); CHECK(r.gen.dimension == std::string("hollow"));
    CHECK_EQ(r.tick, m.tick); CHECK(r.gameMode == std::string("creative"));
}
