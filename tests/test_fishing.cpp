// Fishing: the bobber's flight and float, bites, reeling, and the catch table (flat world, ground
// top y = 5; a pond dug into it).
#include "testing.h"
#include "game/fishing.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
const ItemRegistry& items() { static ItemRegistry r; static bool ok = [] { std::string e; return r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/items.json", blocks(), nullptr, &e); }(); (void)ok; return r; }
struct Pond {
    World w{blocks(), jobs(), items().names()};
    explicit Pond(const char* n, bool roof = false) {
        WorldMeta m; m.gen.preset = "flat"; std::string err; w.create(vxtest::tempDir(n), m, &err);
        for (int z = -2; z <= 2; z++) for (int x = -2; x <= 2; x++) w.loadChunkSync({x, z});
        for (int x = 4; x < 12; x++) for (int z = -4; z < 4; z++) for (int y = 3; y <= 4; y++) w.setBlock({x, y, z}, blocks().stateOf("water"));
        if (roof) for (int x = 4; x < 12; x++) for (int z = -4; z < 4; z++) w.setBlock({x, 9, z}, blocks().stateOf("stone"));
    }
};
} // namespace

TEST(fishing_bobber_lands_and_floats_on_the_pond) {
    Pond p("fish1");
    Rng rng(5);
    Vec3 eye{0.5f, 6.62f, 0.5f};
    Bobber b = Fishing::cast(eye, normalize(Vec3(1, 0.3f, 0)), rng);
    for (int t = 0; t < 120; t++) Fishing::tick(p.w, b, eye, rng);
    REQUIRE(!b.removed);
    CHECK(b.inWater);
    CHECK(b.pos.x > 4 && b.pos.x < 12);
    CHECK(std::fabs(b.pos.y - 4.8f) < 0.15f);   // riding the surface of the 2-deep pond
    // cast onto dry ground: it rests on the grass and never bites
    Bobber g = Fishing::cast(eye, normalize(Vec3(-1, 0.3f, 0)), rng);
    bool bit = false;
    for (int t = 0; t < 2000; t++) bit = Fishing::tick(p.w, g, eye, rng) || bit;
    CHECK(!bit); CHECK(!g.inWater);
    CHECK(std::fabs(g.pos.y - (5 + Fishing::BOX)) < 0.02f);
    CHECK(Fishing::reel(g, items(), rng).empty());
    CHECK(g.removed);
}

TEST(fishing_bites_come_and_go_and_only_a_timely_reel_catches) {
    Pond p("fish2");
    Rng rng(9);
    Vec3 eye{0.5f, 6.62f, 0.5f};
    Bobber b = Fishing::cast(eye, normalize(Vec3(1, 0.3f, 0)), rng);
    int bites = 0, firstBite = -1;
    for (int t = 0; t < 4000; t++) if (Fishing::tick(p.w, b, eye, rng)) { bites++; if (firstBite < 0) firstBite = t; }
    CHECK(bites >= 4);                                   // several bites while left in the water
    CHECK(firstBite >= Fishing::MIN_WAIT);
    // reel exactly during a bite: a catch; reel between bites: nothing
    Bobber c = Fishing::cast(eye, normalize(Vec3(1, 0.3f, 0)), rng);
    while (!Fishing::tick(p.w, c, eye, rng)) {}
    ItemStack got = Fishing::reel(c, items(), rng);
    CHECK(!got.empty());
    Bobber m = Fishing::cast(eye, normalize(Vec3(1, 0.3f, 0)), rng);
    for (int t = 0; t < 60; t++) Fishing::tick(p.w, m, eye, rng);
    REQUIRE(m.bite == 0);
    CHECK(Fishing::reel(m, items(), rng).empty());
    // walking too far away snaps the line
    Bobber f = Fishing::cast(eye, normalize(Vec3(1, 0.3f, 0)), rng);
    for (int t = 0; t < 50; t++) Fishing::tick(p.w, f, eye, rng);
    Fishing::tick(p.w, f, eye + Vec3(-40, 0, 0), rng);
    CHECK(f.removed);
}

TEST(fishing_covered_water_bites_slower_and_catch_table_is_mostly_fish) {
    auto biteCount = [](bool roof) {
        Pond p(roof ? "fish3r" : "fish3o", roof);
        Rng rng(21);
        Vec3 eye{0.5f, 6.62f, 0.5f};
        Bobber b = Fishing::cast(eye, normalize(Vec3(1, 0.3f, 0)), rng);
        int n = 0;
        for (int t = 0; t < 20000; t++) if (Fishing::tick(p.w, b, eye, rng)) n++;
        return n;
    };
    int open = biteCount(false), covered = biteCount(true);
    CHECK(open > 0);
    CHECK(covered * 3 < open * 2);                       // about half as many under a roof
    // every table entry is a real item, and the weights give ~75% fish
    for (auto& e : Fishing::table()) CHECK(items().id(e.item) != 0);
    Rng rng(1); int fish = 0, N = 4000;
    for (int i = 0; i < N; i++) {
        Bobber b; b.inWater = true; b.bite = 1;
        std::string n = items().def(Fishing::reel(b, items(), rng).item).name;
        if (n == "silverfin" || n == "glowgill") fish++;
    }
    CHECK(fish > N * 70 / 100 && fish < N * 80 / 100);
}
