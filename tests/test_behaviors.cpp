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
