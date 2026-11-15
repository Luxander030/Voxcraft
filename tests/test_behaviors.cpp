// Fluid flow, falling blocks and their persistence across save/reload (flat world, surface y = 5).
#include "testing.h"
#include "game/behaviors.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
struct Env {
    World w{blocks(), jobs(), vxtest::testItems()};
    std::unique_ptr<BlockBehaviors> b;
    explicit Env(const fs::path& dir, bool create = true) {
        std::string err;
        if (create) { WorldMeta m; m.gen.preset = "flat"; w.create(dir, m, &err); } else w.open(dir, &err);
        for (int z = -2; z <= 2; z++) for (int x = -2; x <= 2; x++) w.loadChunkSync({x, z});
        b = std::make_unique<BlockBehaviors>(w);
    }
    void run(int ticks) { for (int i = 0; i < ticks; i++) w.tick(); }
    void put(int x, int y, int z, BlockState s) { w.setBlock({x, y, z}, s); }
    bool water(int x, int y, int z) { return blocks().def(w.getBlock({x, y, z})).name == "water"; }
    int level(int x, int y, int z) { return blocks().getProp(w.getBlock({x, y, z}), "level"); }
    std::string snapshot() {   // water layout around the origin
        std::string s;
        for (int y = 2; y <= 6; y++) for (int z = -12; z <= 12; z++) for (int x = -12; x <= 12; x++) s += water(x, y, z) ? (char)('0' + level(x, y, z)) : '.';
        return s;
    }
};
} // namespace

TEST(fluids_water_spreads_seven_and_dries_up) {
    Env e(vxtest::tempDir("fl1"));
    e.b->placeFluidSource({0, 5, 0}, false);
    e.run(200);
    CHECK(e.water(0, 5, 0)); CHECK_EQ(e.level(0, 5, 0), 0);
    CHECK(e.water(7, 5, 0)); CHECK_EQ(e.level(7, 5, 0), 7);
    CHECK(!e.water(8, 5, 0));
    CHECK(e.water(3, 5, 4));    // diamond-shaped spread: |dx|+|dz| <= 7
    CHECK(!e.water(4, 5, 4));
    e.w.setBlock({0, 5, 0}, AIR);   // remove the source
    e.run(200);
    for (int x = -8; x <= 8; x++) CHECK(!e.water(x, 5, 0));
}

TEST(fluids_water_falls_into_holes_and_renews) {
    Env e(vxtest::tempDir("fl2"));
    for (int y = 2; y <= 4; y++) e.w.setBlock({3, y, 0}, AIR);      // 3-deep shaft at x = 3
    e.b->placeFluidSource({0, 5, 0}, false);
    e.run(300);
    CHECK(e.water(3, 2, 0));          // reached the bottom
    CHECK_EQ(e.level(3, 3, 0), 8);    // falling column
    // two sources with a gap make the gap a source
    Env f(vxtest::tempDir("fl3"));
    f.b->placeFluidSource({0, 5, 0}, false); f.b->placeFluidSource({2, 5, 0}, false);
    f.run(100);
    CHECK_EQ(f.level(1, 5, 0), 0);
}

TEST(fluids_lava_meets_water_and_sand_falls) {
    Env e(vxtest::tempDir("fl4"));
    e.b->placeFluidSource({0, 5, 0}, true);
    e.run(200);
    CHECK(blocks().def(e.w.getBlock({2, 5, 0})).name == "lava");   // lava spreads 3 (step 2)
    CHECK(blocks().def(e.w.getBlock({4, 5, 0})).name != "lava");
    e.b->placeFluidSource({6, 5, 0}, false);
    e.run(300);
    std::string n = blocks().def(e.w.getBlock({0, 5, 0})).name;
    CHECK(n == "slate" || n == "lava");   // source hardens if water reached it
    bool hardened = false;
    for (int x = 0; x <= 4; x++) { std::string m = blocks().def(e.w.getBlock({x, 5, 0})).name; if (m == "cobblestone" || m == "slate") hardened = true; }
    CHECK(hardened);
    // sand column falls to the ground
    e.w.setBlock({-5, 12, -5}, blocks().stateOf("sand"));
    e.run(40);
    CHECK(blocks().def(e.w.getBlock({-5, 5, -5})).name == "sand");
    CHECK(e.w.getBlock({-5, 12, -5}) == AIR);
}

TEST(fluids_flow_resumes_identically_after_reload) {
    std::string uninterrupted, resumed;
    {
        Env e(vxtest::tempDir("fl5a"));
        e.b->placeFluidSource({0, 5, 0}, false);
        e.run(400);
        uninterrupted = e.snapshot();
    }
    auto dir = vxtest::tempDir("fl5b");
    {
        Env e(dir);
        e.b->placeFluidSource({0, 5, 0}, false);
        e.run(17);                                   // mid-flow: ticks pending
        std::string err; REQUIRE(e.w.save(true, {}, &err));
        e.b.reset(); e.w.close();
    }
    {
        Env e(dir, false);
        e.run(400 - 17);
        resumed = e.snapshot();
    }
    CHECK(uninterrupted == resumed);
}

#include "game/particles.h"
TEST(particles_fall_settle_expire_and_cap) {
    Env e(vxtest::tempDir("parts"));
    ParticleSystem ps; Rng rng(2);
    ps.blockDebris({0, 8, 0}, 1, 0xFFFFFFFFu, 20, rng);
    CHECK_EQ(ps.parts.size(), (size_t)20);
    for (int i = 0; i < 8; i++) ps.tick(e.w);
    for (auto& p : ps.parts) CHECK(p.pos.y >= 5.0f - 1e-3f);   // never fall through the ground (top at y=5)
    for (int i = 0; i < 40; i++) ps.tick(e.w);
    CHECK(ps.parts.empty());                                    // all expired
    for (int i = 0; i < 300; i++) ps.blockDebris({0, 8, 0}, 1, 0, 20, rng);
    CHECK_EQ(ps.parts.size(), ParticleSystem::CAPACITY);         // bounded
}

TEST(rifts_light_inside_closed_slate_frames_and_collapse) {
    Env e(vxtest::tempDir("rift"));
    BlockState slate = blocks().stateOf("slate");
    // 4x5 frame in the x-y plane at z = 0 (interior 2x3 at x 1..2, y 6..8)
    for (int x = 0; x <= 3; x++) for (int y = 5; y <= 9; y++) if (x == 0 || x == 3 || y == 5 || y == 9) e.put(x, y, 0, slate);
    CHECK_EQ(e.b->lightRift({1, 6, 0}), 6);
    CHECK(blocks().def(e.w.getBlock({2, 8, 0})).name == "rift");
    // open frame: nothing happens
    Env f(vxtest::tempDir("rift2"));
    for (int x = 0; x <= 3; x++) for (int y = 5; y <= 9; y++) if ((x == 0 || x == 3 || y == 5 || y == 9) && !(x == 3 && y == 7)) f.put(x, y, 0, slate);
    CHECK_EQ(f.b->lightRift({1, 6, 0}), 0);
    // breaking the frame collapses the whole rift
    e.w.setBlock({3, 7, 0}, AIR);
    for (int x = 1; x <= 2; x++) for (int y = 6; y <= 8; y++) CHECK(e.w.getBlock({x, y, 0}) == AIR);
    // built gates are valid and lit
    e.b->buildRiftGate({10, 6, 10}, false);
    CHECK(blocks().def(e.w.getBlock({10, 7, 11})).name == "rift");
}

TEST(hollow_dimension_generates_sealed_caverns) {
    GenSettings s; s.seed = 5; s.version = 2; s.dimension = "hollow";
    auto g = createGenerator(s, blocks());
    REQUIRE(g);
    GenSettings old = s; old.version = 1;
    CHECK(!createGenerator(old, blocks()));   // the Hollow did not exist in v1
    Chunk c({2, -3}); g->generate(c);
    int air = 0, lava = 0, rock = 0;
    for (int y = 1; y < 127; y++) for (int z = 0; z < 16; z++) for (int x = 0; x < 16; x++) {
        std::string n = blocks().def(c.get(x, y, z)).name;
        air += n == "air"; lava += n == "lava"; rock += n == "hollowrock" || n == "ashsoil" || n == "ember_ore";
    }
    CHECK(blocks().def(c.get(5, 0, 5)).name == "bedrock" && blocks().def(c.get(5, 127, 5)).name == "bedrock");
    CHECK(air > 1000 && rock > 1000);
    printf("    hollow chunk: air %d, lava %d, rock %d\n", air, lava, rock);
    Chunk d({2, -3}); g->generate(d);
    for (int s2 = 0; s2 < SECTIONS; s2++) for (int i = 0; i < 4096; i++) if (c.sec[s2].get(i) != d.sec[s2].get(i)) { CHECK(false); s2 = SECTIONS; break; }
}
