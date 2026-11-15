// Glimmer circuit behaviour and timing (flat world: ground top at y = 4, place things at y = 5).
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
    void run(int t) { for (int i = 0; i < t; i++) w.tick(); }
    BlockState st(const char* n) { return blocks().stateOf(n); }
    BlockState facing(const char* n, int f) { return blocks().withProp(st(n), "facing", f); }
    void put(int x, int y, int z, BlockState s) { w.setBlock({x, y, z}, s); }
    int prop(int x, int y, int z, const char* p) { return blocks().getProp(w.getBlock({x, y, z}), p); }
    std::string name(int x, int y, int z) { return blocks().def(w.getBlock({x, y, z})).name; }
};
} // namespace

TEST(circuits_wire_decays_and_drives_lamp) {
    Env e(vxtest::tempDir("c1"));
    e.put(0, 5, 0, e.st("lever"));
    for (int x = 1; x <= 14; x++) e.put(x, 5, 0, e.st("glimmer_wire"));
    e.put(15, 5, 0, e.st("lamp"));
    e.b->toggleLever({0, 5, 0});
    e.run(4);
    CHECK_EQ(e.prop(1, 5, 0, "power"), 15);
    CHECK_EQ(e.prop(14, 5, 0, "power"), 2);
    CHECK_EQ(e.prop(15, 5, 0, "lit"), 1);
    e.b->toggleLever({0, 5, 0});
    CHECK_EQ(e.prop(14, 5, 0, "power"), 0);       // wire turns off immediately
    e.run(1); CHECK_EQ(e.prop(15, 5, 0, "lit"), 1);
    e.run(2); CHECK_EQ(e.prop(15, 5, 0, "lit"), 0);   // lamp off after 2 ticks
    // too long: 17 wire blocks do not reach
    e.put(0, 5, 3, e.st("lever"));
    for (int x = 1; x <= 16; x++) e.put(x, 5, 3, e.st("glimmer_wire"));
    e.put(17, 5, 3, e.st("lamp"));
    e.b->toggleLever({0, 5, 3}); e.run(4);
    CHECK_EQ(e.prop(16, 5, 3, "power"), 0);
    CHECK_EQ(e.prop(17, 5, 3, "lit"), 0);
}

TEST(circuits_torch_inverts_and_relay_delays) {
    Env e(vxtest::tempDir("c2"));
    // lever -> relay (delay index 0 = 2 ticks) -> stone B -> torch on B -> lamp beside torch
    e.put(-2, 5, 0, e.st("lever"));
    e.put(-1, 5, 0, e.facing("relay", 3));      // output east
    e.put(0, 5, 0, e.st("stone"));
    e.put(0, 6, 0, e.st("spark_torch"));
    e.put(1, 6, 0, e.st("lamp"));
    e.run(6);
    CHECK_EQ(e.prop(0, 6, 0, "lit"), 1);
    CHECK_EQ(e.prop(1, 6, 0, "lit"), 1);
    e.b->toggleLever({-2, 5, 0});
    int t = 0;
    while (e.prop(1, 6, 0, "lit") == 1 && t < 40) { e.run(1); t++; }
    CHECK_EQ(e.prop(-1, 5, 0, "powered"), 1);
    CHECK_EQ(e.prop(0, 6, 0, "lit"), 0);
    CHECK_EQ(t, 2 + 2 + 2);   // relay 2, torch 2, lamp-off 2
    // longer delay: index 3 -> 8 ticks
    Env f(vxtest::tempDir("c3"));
    f.put(0, 5, 0, f.st("lever"));
    f.put(1, 5, 0, f.w.reg.withProp(f.facing("relay", 3), "delay", 3));
    f.put(2, 5, 0, f.st("lamp"));
    f.b->toggleLever({0, 5, 0});
    int u = 0; while (f.prop(2, 5, 0, "lit") == 0 && u < 40) { f.run(1); u++; }
    CHECK_EQ(u, 8);
}

TEST(circuits_piston_pushes_and_retracts) {
    Env e(vxtest::tempDir("c4"));
    e.put(0, 5, 0, e.facing("piston", 5));     // pushes +X
    for (int x = 1; x <= 3; x++) e.put(x, 5, 0, e.st("cobblestone"));
    e.put(-1, 5, 0, e.st("lever"));
    e.b->toggleLever({-1, 5, 0});
    e.run(4);
    CHECK_EQ(e.prop(0, 5, 0, "extended"), 1);
    CHECK(e.name(1, 5, 0) == "piston_head");
    CHECK(e.name(4, 5, 0) == "cobblestone");
    CHECK(e.name(2, 5, 0) == "cobblestone");
    e.b->toggleLever({-1, 5, 0});
    e.run(4);
    CHECK_EQ(e.prop(0, 5, 0, "extended"), 0);
    CHECK(e.w.getBlock({1, 5, 0}) == AIR);
    // blocked by bedrock: does not extend
    Env f(vxtest::tempDir("c5"));
    f.put(0, 5, 0, f.facing("piston", 5)); f.put(1, 5, 0, f.st("bedrock")); f.put(-1, 5, 0, f.st("lever"));
    f.b->toggleLever({-1, 5, 0}); f.run(4);
    CHECK_EQ(f.prop(0, 5, 0, "extended"), 0);
}

TEST(circuits_torch_clock_oscillates_and_resumes_after_reload) {
    // Real feedback loop: torch T on block B; T feeds wire W1 (on a pillar) which steps down and runs
    // around to a relay that outputs back into B. B powered -> T off -> wire off -> relay off -> T on.
    auto build = [](Env& e) {
        e.put(0, 5, 0, e.st("stone"));                         // B
        e.put(0, 6, 0, e.st("spark_torch"));                   // T
        e.put(1, 5, 0, e.st("stone")); e.put(1, 6, 0, e.st("glimmer_wire"));   // W1 on a pillar
        e.put(2, 5, 0, e.st("glimmer_wire"));                  // step down
        e.put(2, 5, 1, e.st("glimmer_wire")); e.put(2, 5, 2, e.st("glimmer_wire"));
        e.put(1, 5, 2, e.st("glimmer_wire")); e.put(0, 5, 2, e.st("glimmer_wire"));
        e.put(0, 5, 1, e.facing("relay", 0));                  // back (0,5,2) -> outputs north into B
    };
    std::string a, b;
    auto trace = [](Env& e, int ticks) { std::string s; for (int i = 0; i < ticks; i++) { e.run(1); s += (char)('0' + e.prop(0, 6, 0, "lit")); } return s; };
    {
        Env e(vxtest::tempDir("c6a")); build(e);
        a = trace(e, 120);
    }
    auto dir = vxtest::tempDir("c6b");
    {
        Env e(dir); build(e);
        b = trace(e, 37);
        std::string err; REQUIRE(e.w.save(true, {}, &err));
        e.b.reset(); e.w.close();
    }
    { Env e(dir, false); b += trace(e, 120 - 37); }
    CHECK(a == b);
    printf("    torch trace: %s\n", a.substr(0, 60).c_str());
    int flips = 0; for (size_t i = 1; i < a.size(); i++) if (a[i] != a[i - 1]) flips++;
    CHECK(flips >= 10);   // it really is a clock
}
