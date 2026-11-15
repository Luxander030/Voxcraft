// Rails (shape rules) and carts (rail physics, slopes, powered rails, persistence). Flat world,
// surface y = 5 (ground blocks at y <= 4).
#include "testing.h"
#include "game/behaviors.h"
#include "game/carts.h"
#include "game/entities.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
const ItemRegistry& itemsReg() { static ItemRegistry r; static bool ok = [] { std::string e; return r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/items.json", blocks(), nullptr, &e); }(); (void)ok; return r; }
struct Env {
    World w{blocks(), jobs(), vxtest::testItems()};
    std::unique_ptr<BlockBehaviors> b;
    explicit Env(const char* name) {
        std::string err; WorldMeta m; m.gen.preset = "flat"; w.create(vxtest::tempDir(name), m, &err);
        for (int z = -3; z <= 3; z++) for (int x = -3; x <= 3; x++) w.loadChunkSync({x, z});
        b = std::make_unique<BlockBehaviors>(w);
    }
    void rail(int x, int y, int z, const char* n = "rail") { w.setBlock({x, y, z}, blocks().stateOf(n)); }
    int shape(int x, int y, int z) { return blocks().getProp(w.getBlock({x, y, z}), "shape"); }
};
} // namespace

TEST(rails_connect_straight_curve_and_slope) {
    Env e("rail1");
    for (int x = 0; x <= 4; x++) e.rail(x, 5, 0);
    CHECK_EQ(e.shape(2, 5, 0), 1);                   // east-west line
    e.rail(4, 5, 1);                                  // turn south at the end
    CHECK_EQ(e.shape(4, 5, 0), 7);                   // a bend joining south and west
    CHECK_EQ(e.shape(4, 5, 1), 0);
    // a slope: rail climbing east onto a block
    e.w.setBlock({7, 5, 0}, blocks().stateOf("stone"));
    e.rail(5, 5, 0); e.rail(6, 5, 0); e.rail(7, 6, 0);
    CHECK_EQ(e.shape(6, 5, 0), 5);                   // ascending toward the east
    CHECK_EQ(e.shape(5, 5, 0), 1);
    // losing support removes the rail
    e.w.setBlock({0, 4, 0}, AIR);
    CHECK(e.w.getBlock({0, 5, 0}) == AIR);
}

TEST(carts_roll_curve_climb_and_stop) {
    Env e("cart1");
    for (int x = 0; x <= 10; x++) e.rail(x, 5, 0);
    for (int z = 1; z <= 10; z++) e.rail(10, 5, z);   // bend south at x = 10
    CartSystem cs;
    Cart& c = cs.spawn({0.5f, 5, 0.5f});
    c.vel = {0.3f, 0, 0};
    float maxZ = 0; int ticks = 0;
    for (; ticks < 400; ticks++) { cs.tick(e.w); maxZ = std::max(maxZ, cs.carts[0].pos.z); }
    const Cart& r = cs.carts[0];
    CHECK(maxZ > 5);                                  // went round the bend and down the south leg
    CHECK(std::fabs(r.pos.x - 10.5f) < 0.05f);        // stayed on the rail line
    // a cart at rest stays at rest
    Cart& s = cs.spawn({3.5f, 5, 0.5f});
    for (int t = 0; t < 50; t++) cs.step(e.w, s);
    CHECK(std::fabs(s.pos.x - 3.5f) < 1e-4f);
}

TEST(carts_slopes_and_powered_rails) {
    Env e("cart2");
    // a hill: climb from y=5 to y=8 then a flat top
    for (int k = 0; k < 3; k++) {
        for (int y = 5; y <= 5 + k; y++) e.w.setBlock({4 + k, y, 0}, blocks().stateOf("stone"));
    }
    for (int x = 0; x <= 3; x++) e.rail(x, 5, 0);
    for (int k = 0; k < 3; k++) e.rail(4 + k, 6 + k, 0);
    CHECK_EQ(e.shape(3, 5, 0), 5);
    CartSystem cs;
    // released at the top, a cart rolls down the slope and keeps going west
    for (int x = 7; x <= 9; x++) { for (int y = 5; y <= 7; y++) e.w.setBlock({x, y, 0}, blocks().stateOf("stone")); e.rail(x, 8, 0); }
    Cart& c = cs.spawn({6.5f, 8.0f, 0.5f});
    c.vel = {-0.05f, 0, 0};
    for (int t = 0; t < 120; t++) cs.tick(e.w);
    CHECK(cs.carts[0].pos.x < 3);                     // gravity carried it down and along
    CHECK(cs.carts[0].pos.y < 5.1f);
    // powered rail: boosts when powered, brakes when not
    Env p("cart3");
    for (int x = 0; x <= 30; x++) p.rail(x, 5, 3);
    p.w.setBlock({5, 5, 3}, blocks().stateOf("powered_rail"));
    p.w.setBlock({5, 5, 4}, blocks().stateOf("lever"));
    p.b->toggleLever({5, 5, 4});
    for (int i = 0; i < 4; i++) p.w.tick();
    REQUIRE(blocks().getProp(p.w.getBlock({5, 5, 3}), "powered") == 1);
    CartSystem ps;
    Cart& q = ps.spawn({1.5f, 5, 3.5f}); q.vel = {0.05f, 0, 0};
    for (int t = 0; t < 200 && ps.carts[0].pos.x < 7; t++) ps.tick(p.w);
    CHECK(length(ps.carts[0].vel) > 0.08f);          // boosted
    p.b->toggleLever({5, 5, 4});
    for (int i = 0; i < 4; i++) p.w.tick();
    Cart& q2 = ps.spawn({1.5f, 5, 3.5f}); q2.vel = {0.2f, 0, 0};
    float before = 0.2f;
    for (int t = 0; t < 40 && ps.carts.back().pos.x < 6.5f; t++) ps.tick(p.w);
    CHECK(length(ps.carts.back().vel) < before * 0.6f);   // braked by the unpowered rail
}

TEST(carts_persist_with_their_chunk) {
    CartSystem cs;
    EntityManager em(itemsReg()); em.carts = &cs;
    Cart& c = cs.spawn({3.5f, 5.0f, 4.5f}); c.vel = {0.1f, 0, 0}; c.yaw = 1.0f;
    auto recs = em.collectEntities({0, 0}, true);
    CHECK(cs.carts.empty());
    REQUIRE(recs.size() == 1);
    em.spawnEntities({0, 0}, std::move(recs));
    REQUIRE(cs.carts.size() == 1);
    CHECK(std::fabs(cs.carts[0].pos.x - 3.5f) < 1e-5f); CHECK(std::fabs(cs.carts[0].vel.x - 0.1f) < 1e-6f);
}

TEST(carts_circle_a_closed_loop_without_derailing) {
    Env e("cartloop");
    for (int x = 0; x <= 6; x++) for (int z = 0; z <= 6; z++) if (x == 0 || x == 6 || z == 0 || z == 6) e.rail(x, 5, z);
    CHECK_EQ(e.shape(0, 5, 0), 6); CHECK_EQ(e.shape(6, 5, 0), 7); CHECK_EQ(e.shape(6, 5, 6), 8); CHECK_EQ(e.shape(0, 5, 6), 9);
    CartSystem cs;
    Cart& c = cs.spawn({3.5f, 5, 0.5f}); c.vel = {0.3f, 0, 0};
    int corners = 0; int lastQuadrant = -1;
    for (int t = 0; t < 600; t++) {
        cs.tick(e.w);
        const Cart& k = cs.carts[0];
        REQUIRE(k.onRail);
        int cx = (int)std::floor(k.pos.x), cz = (int)std::floor(k.pos.z);
        CHECK((cx == 0 || cx == 6 || cz == 0 || cz == 6));   // always on the loop
        int q = (cx >= 3 ? 1 : 0) + (cz >= 3 ? 2 : 0);
        if (q != lastQuadrant) { corners++; lastQuadrant = q; }
    }
    CHECK(corners > 8);   // went round more than twice
}
