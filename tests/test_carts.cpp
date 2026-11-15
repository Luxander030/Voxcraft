// Rails (shape rules) and carts (rail physics, slopes, powered rails, persistence). Flat world,
// surface y = 5 (ground blocks at y <= 4).
#include "testing.h"
#include "game/behaviors.h"
#include "game/carts.h"
#include "game/entities.h"
#include "game/mobs.h"
#include "core/util.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
const ItemRegistry& itemsReg() { static ItemRegistry r; static bool ok = [] { std::string e; return r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/items.json", blocks(), nullptr, &e); }(); (void)ok; return r; }
Json loadJson(const char* rel) { auto t = readTextFile(fs::path(VX_SOURCE_DIR) / rel); std::string e; return Json::parse(t ? *t : "{}", &e); }
const MobRegistry& mobReg() {
    static MobRegistry r;
    static bool ok = [] { std::string e; return r.load(loadJson("assets/data/mobs.json"), loadJson("assets/generated/models.json"), loadJson("assets/data/animations.json"), itemsReg(), nullptr, &e); }();
    (void)ok; return r;
}
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

TEST(carts_collide_and_hand_over_speed) {
    Env e("cartbump");
    for (int x = 0; x <= 30; x++) e.rail(x, 5, 0);
    CartSystem cs;
    cs.spawn({2.5f, 5, 0.5f}).vel = {0.3f, 0, 0};
    cs.spawn({8.5f, 5, 0.5f});
    float minGap = 10;
    for (int t = 0; t < 60; t++) { cs.tick(e.w); minGap = std::min(minGap, cs.carts[1].pos.x - cs.carts[0].pos.x); }
    CHECK(minGap > 0.5f);                             // never passed through each other
    CHECK(cs.carts[1].pos.x > 12);                    // the struck cart took over the motion
    CHECK(std::fabs(cs.carts[0].vel.x) < 0.05f);      // and the striker came (nearly) to rest
    // two carts dropped on the same spot separate along the track
    CartSystem two;
    two.spawn({20.5f, 5, 0.5f}); two.spawn({20.6f, 5, 0.5f});
    for (int t = 0; t < 100; t++) two.tick(e.w);
    CHECK(std::fabs(two.carts[0].pos.x - two.carts[1].pos.x) > 0.9f);
    CHECK(std::fabs(two.carts[0].pos.z - 0.5f) < 1e-3f);   // still on the rail line
}

TEST(carts_knock_creatures_aside_and_players_push_carts) {
    Env e("cartmob");
    for (int x = 0; x <= 30; x++) e.rail(x, 5, 0);
    MobSystem ms(mobReg()); Rng rng(7);
    Mob& m = ms.spawn(mobReg().id("grazer"), {8.5f, 5, 0.5f}, rng);
    m.vel = {};
    CartSystem cs;
    cs.spawn({2.5f, 5, 0.5f}).vel = {0.3f, 0, 0};
    float speedAtHit = 0; bool hit = false;
    for (int t = 0; t < 40 && !hit; t++) {
        cs.tick(e.w);
        float v = cs.carts[0].vel.x;
        collideCartsWithMobs(cs, ms);
        if (length(ms.mobs[0].vel) > 0.1f) { hit = true; speedAtHit = v; }
    }
    REQUIRE(hit);
    CHECK(ms.mobs[0].vel.x > 0.1f);                   // shoved ahead of the cart
    cs.tick(e.w);
    CHECK(cs.carts[0].vel.x < speedAtHit);            // the cart lost some speed
    CHECK(cs.carts[0].vel.x > speedAtHit * 0.5f);     // but not most of it
    // a player leaning on a resting cart from the west walks it east, along the track only
    CartSystem ps;
    ps.spawn({15.5f, 5, 0.5f});
    for (int t = 0; t < 40; t++) {
        Vec3 c = ps.carts[0].pos;
        AABB player{{c.x - 1.2f, 5, c.z - 0.2f}, {c.x - 0.4f, 6.8f, c.z + 0.5f}};   // overlapping its west side, a bit north
        collideCartsWithPlayer(ps, player, {0.1f, 0, 0}, 0);
        ps.tick(e.w);
    }
    CHECK(ps.carts[0].pos.x > 16);
    CHECK(std::fabs(ps.carts[0].pos.z - 0.5f) < 1e-3f);
}

TEST(detector_rail_signals_while_a_cart_is_on_it) {
    Env e("detector");
    for (int x = 0; x <= 12; x++) e.rail(x, 5, 0);
    e.w.setBlock({6, 5, 0}, blocks().stateOf("detector_rail"));
    e.w.setBlock({7, 5, 0}, blocks().stateOf("powered_rail"));
    for (int i = 0; i < 4; i++) e.w.tick();
    CHECK_EQ(e.shape(6, 5, 0), 1);                                  // shaped like any rail
    CHECK_EQ(blocks().getProp(e.w.getBlock({7, 5, 0}), "powered"), 0);
    CartSystem cs;
    cs.onRail = [&](const BlockPos& p, BlockState s) { if (isDetectorRail(blocks(), s)) e.b->cartOnDetector(p); };
    e.b->cartOnRail = [&](const BlockPos& p) {
        for (auto& c : cs.carts) { BlockPos cell; BlockState s; if (cs.railCell(e.w, c.pos, cell, s) && cell == p) return true; }
        return false;
    };
    cs.spawn({6.5f, 5, 0.5f});                                      // parked on the detector
    for (int t = 0; t < 60; t++) { cs.tick(e.w); e.w.tick(); }
    CHECK_EQ(blocks().getProp(e.w.getBlock({6, 5, 0}), "on"), 1);  // stays on while occupied
    CHECK_EQ(blocks().getProp(e.w.getBlock({7, 5, 0}), "powered"), 1);
    cs.carts.clear();                                               // the cart leaves
    for (int t = 0; t < 30; t++) e.w.tick();
    CHECK_EQ(blocks().getProp(e.w.getBlock({6, 5, 0}), "on"), 0);
    CHECK_EQ(blocks().getProp(e.w.getBlock({7, 5, 0}), "powered"), 0);
}

TEST(storage_carts_persist_contents_and_drop_them_when_broken) {
    CartSystem cs;
    EntityManager em(itemsReg()); em.carts = &cs;
    Cart& c = cs.spawn({3.5f, 5.0f, 4.5f}, CartKind::Storage);
    REQUIRE(c.items.size() == (size_t)CartSystem::STORAGE_SLOTS);
    c.items[0] = {itemsReg().id("coal"), 12, 0};
    c.items[26] = {itemsReg().id("iron_pickaxe"), 1, 37};
    cs.spawn({8.5f, 5.0f, 4.5f});                            // a plain cart alongside
    auto recs = em.collectEntities({0, 0}, true);
    REQUIRE(recs.size() == 2);
    em.spawnEntities({0, 0}, std::move(recs));
    REQUIRE(cs.carts.size() == 2);
    const Cart r = cs.carts[0].kind == CartKind::Storage ? cs.carts[0] : cs.carts[1];   // copies: spawning below reallocates
    const Cart p = cs.carts[0].kind == CartKind::Storage ? cs.carts[1] : cs.carts[0];
    CHECK(r.kind == CartKind::Storage); CHECK(p.kind == CartKind::Plain); CHECK(p.items.empty());
    CHECK_EQ(r.items[0].count, 12); CHECK_EQ(r.items[26].damage, 37);
    CHECK(r.items[26].item == itemsReg().id("iron_pickaxe"));
    // version 1 records (before storage carts) still load as plain carts
    ByteWriter w; w.u8_(1); w.f64_(1.5); w.f64_(5); w.f64_(1.5); w.f32_(0); w.f32_(0); w.f32_(0); w.f32_(0);
    std::vector<EntityRecord> old; old.push_back({"cart", std::move(w.buf)});
    em.spawnEntities({0, 0}, std::move(old));
    CHECK(cs.carts.size() == 3 && cs.carts[2].kind == CartKind::Plain);
    // breaking: survival gets the cart item back plus the contents; creative only the contents
    auto drops = cartDrops(r, itemsReg(), false);
    REQUIRE(drops.size() == 3);
    CHECK(drops[0].item == itemsReg().id("storage_cart"));
    CHECK_EQ(cartDrops(r, itemsReg(), true).size(), (size_t)2);
    CHECK(cartDrops(p, itemsReg(), false)[0].item == itemsReg().id("cart"));
    // hopper carts round-trip too
    CartSystem hs; EntityManager hm(itemsReg()); hm.carts = &hs;
    hs.spawn({1.5f, 5, 1.5f}, CartKind::Hopper).items[4] = {itemsReg().id("stick"), 7, 0};
    auto hr = hm.collectEntities({0, 0}, true);
    hm.spawnEntities({0, 0}, std::move(hr));
    REQUIRE(hs.carts.size() == 1);
    CHECK(hs.carts[0].kind == CartKind::Hopper); CHECK_EQ(hs.carts[0].items.size(), (size_t)CartSystem::HOPPER_SLOTS);
    CHECK_EQ(hs.carts[0].items[4].count, 7);
    CHECK(cartDrops(hs.carts[0], itemsReg(), false)[0].item == itemsReg().id("hopper_cart"));
}
