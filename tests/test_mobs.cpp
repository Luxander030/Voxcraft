// Models, animation, pathfinding, mob AI, spawning and persistence (flat world, surface y = 5).
#include "testing.h"
#include "game/entities.h"
#include "core/util.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
Json loadJson(const char* rel) { auto t = readTextFile(fs::path(VX_SOURCE_DIR) / rel); std::string e; return Json::parse(t ? *t : "{}", &e); }
const ItemRegistry& items() { static ItemRegistry r; static bool ok = [] { std::string e; return r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/items.json", blocks(), nullptr, &e); }(); (void)ok; return r; }
const MobRegistry& mobs() {
    static MobRegistry r;
    static bool ok = [] { std::string e; bool b = r.load(loadJson("assets/data/mobs.json"), loadJson("assets/generated/models.json"), loadJson("assets/data/animations.json"), items(), nullptr, &e); if (!b) fprintf(stderr, "mobs: %s\n", e.c_str()); return b; }();
    (void)ok; return r;
}
struct Flat {
    World w{blocks(), jobs(), items().names()};
    Flat(const char* n) { WorldMeta m; m.gen.preset = "flat"; std::string err; w.create(vxtest::tempDir(n), m, &err); for (int z = -3; z <= 3; z++) for (int x = -3; x <= 3; x++) w.loadChunkSync({x, z}); }
};
} // namespace

TEST(anim_models_load_and_pivots_are_fixed_points) {
    REQUIRE(mobs().size() == 5);
    const Model& m = mobs().model(mobs().def(mobs().id("grazer")).model);
    CHECK(m.bone("head") > 0); CHECK(m.boxes.size() >= 8);
    Animator a; a.bind(&m, &mobs().clips(mobs().id("grazer")));
    a.pose.rot[m.bone("leg_fl")] = {0.7f, 0, 0};
    std::vector<Mat4> mats; a.boneMatrices(mats);
    Vec3 piv = m.bones[m.bone("leg_fl")].pivot;
    Vec4 r = mats[m.bone("leg_fl")] * Vec4{piv.x, piv.y, piv.z, 1};
    CHECK(std::fabs(r.x - piv.x) < 1e-4f && std::fabs(r.y - piv.y) < 1e-4f && std::fabs(r.z - piv.z) < 1e-4f);
    Vec4 foot = mats[m.bone("leg_fl")] * Vec4{piv.x, 0, piv.z, 1};
    CHECK(foot.z < piv.z - 1);   // rotating the leg forward (+X angle) swings the foot... away from rest
}

TEST(anim_clip_interpolation_and_state_machine) {
    u16 g = mobs().id("gloomling");
    const Model& m = mobs().model(mobs().def(g).model);
    const Clip& c = mobs().clips(g).at("attack");
    Pose p; p.reset(m.bones.size());
    c.sample(0.1f, 1.0f, p);   // halfway to -100 deg at t=0.2
    CHECK(std::fabs(p.rot[m.bone("arm_l")].x - (-50.0f * PI / 180.0f)) < 1e-3f);
    Animator a; a.bind(&m, &mobs().clips(g));
    a.update(0.05f, {0.1f, true, 0, 0});
    CHECK(a.state() == AnimState::Walk);
    a.play(AnimState::Attack);
    for (int i = 0; i < 20; i++) a.update(0.05f, {0, true, 0, 0});
    CHECK(a.state() == AnimState::Idle);   // one-shot returned to locomotion
    a.play(AnimState::Death); a.play(AnimState::Walk);
    CHECK(a.state() == AnimState::Death);  // terminal
}

TEST(mobs_pathfinding_routes_around_walls) {
    Flat f("mp");
    for (int z = -4; z <= 4; z++) for (int y = 5; y <= 7; y++) f.w.setBlock({3, y, z}, blocks().stateOf("stone"));
    auto path = findPath(f.w, {0, 5, 0}, {6, 5, 0}, 2);
    REQUIRE(!path.empty());
    for (auto& p : path) CHECK(p.x != 3 || std::abs(p.z) > 4);
    CHECK(path.size() > 10);   // had to go around
    for (int z = 18; z <= 22; z++) for (int x = 18; x <= 22; x++) for (int y = 5; y <= 7; y++)
        if (z == 18 || z == 22 || x == 18 || x == 22) f.w.setBlock({x, y, z}, blocks().stateOf("stone"));   // closed room
    CHECK(findPath(f.w, {0, 5, 0}, {20, 5, 20}, 2, 300).empty());   // unreachable: search gives up within budget
    auto again = findPath(f.w, {0, 5, 0}, {1, 5, 5}, 2);
    CHECK(again == findPath(f.w, {0, 5, 0}, {1, 5, 5}, 2));   // deterministic
}

TEST(mobs_hostile_chases_and_attacks_player_passive_panics) {
    Flat f("mc");
    MobSystem ms(mobs()); Rng rng(5);
    Player pl; pl.pos = {8.5f, 5, 0.5f};
    float dealt = 0;
    MobContext ctx{f.w, pl, true, 0.2f, [&](Mob&, float d) { dealt += d; }, nullptr};
    ms.spawn(mobs().id("gloomling"), {0.5f, 5, 0.5f}, rng);
    for (int i = 0; i < 200 && dealt == 0; i++) ms.tick(ctx, rng);
    CHECK(dealt > 0);
    Mob& g = ms.mobs[0];
    Vec3 d = g.pos - pl.pos; CHECK(std::sqrt(d.x * d.x + d.z * d.z) < 2.0f);
    // passive mob flees after being hit
    Mob& gr = ms.spawn(mobs().id("grazer"), {-5.5f, 5, 0.5f}, rng);
    size_t gi = ms.mobs.size() - 1;
    Vec3 start = gr.pos;
    CHECK(ms.hurt(gr, 2, {-1, 0, 0}, rng, &ctx));
    CHECK(!ms.hurt(ms.mobs[gi], 2, {-1, 0, 0}, rng, &ctx));   // invulnerable for a moment
    for (int i = 0; i < 60; i++) ms.tick(ctx, rng);
    Vec3 moved = ms.mobs[gi].pos - start;
    CHECK(std::sqrt(moved.x * moved.x + moved.z * moved.z) > 2.0f);
}

TEST(mobs_die_drop_loot_and_daylight_burns) {
    Flat f("md");
    MobSystem ms(mobs()); Rng rng(9);
    Player pl; pl.pos = {30.5f, 5, 30.5f};
    std::vector<ItemStack> got;
    MobContext ctx{f.w, pl, true, 1.0f, nullptr, [&](const Mob&, const std::vector<ItemStack>& l) { got.insert(got.end(), l.begin(), l.end()); }};
    ms.spawn(mobs().id("grazer"), {0.5f, 5, 0.5f}, rng);
    for (int i = 0; i < 10; i++) { ms.mobs[0].hurtTicks = 0; ms.hurt(ms.mobs[0], 5, {1, 0, 0}, rng, &ctx); }
    CHECK(!ms.mobs[0].alive());
    CHECK(!got.empty());
    for (int i = 0; i < 30; i++) ms.tick(ctx, rng);
    CHECK(ms.mobs.empty());   // removed after the death animation
    ms.spawn(mobs().id("gloomling"), {0.5f, 5, 0.5f}, rng);   // in full daylight
    for (int i = 0; i < 400 && !ms.mobs.empty(); i++) ms.tick(ctx, rng);
    CHECK(ms.mobs.empty() || !ms.mobs[0].alive());
}

TEST(mobs_spawn_rules_and_persist_with_chunk) {
    auto dir = vxtest::tempDir("mpersist");
    MobSystem ms(mobs()); EntityManager em(items()); em.mobs = &ms;
    {
        World w(blocks(), jobs(), items().names()); w.setEntityHost(&em);
        WorldMeta m; m.gen.preset = "flat"; std::string err; REQUIRE(w.create(dir, m, &err));
        for (int z = -4; z <= 4; z++) for (int x = -4; x <= 4; x++) w.loadChunkSync({x, z});
        Player pl; pl.pos = {0.5f, 5, 0.5f}; Rng rng(11);
        for (int i = 0; i < 200; i++) ms.spawnTick(w, pl, 1.0f, rng);
        REQUIRE(!ms.mobs.empty());
        for (auto& mob : ms.mobs) CHECK(mobs().def(mob.type).behavior == MobBehavior::Passive);   // daytime surface: passives only
        CHECK(ms.countNear(pl.pos, 64, MobBehavior::Passive) <= 10);
        size_t n = ms.mobs.size();
        w.close();          // final save records the mobs with their chunks
        ms.mobs.clear();    // (a new session starts empty)
        World w2(blocks(), jobs(), items().names()); w2.setEntityHost(&em);
        REQUIRE(w2.open(dir, &err));
        for (int z = -4; z <= 4; z++) for (int x = -4; x <= 4; x++) w2.loadChunkSync({x, z});
        CHECK_EQ(ms.mobs.size(), n);
        w2.close();
    }
}

#include "game/projectiles.h"
TEST(projectiles_hit_mobs_stick_in_blocks_and_hit_player) {
    Flat f("proj");
    MobSystem ms(mobs()); Rng rng(1);
    ProjectileSystem ps;
    Player pl; pl.pos = {0.5f, 5, 0.5f};
    float mobDmg = 0, playerDmg = 0; int picked = 0;
    ProjectileHooks h;
    h.hitMob = [&](Mob& m, float d, Vec3 dir) { mobDmg += d; ms.hurt(m, d, dir, rng, nullptr); };
    h.hitPlayer = [&](float d, Vec3) { playerDmg += d; };
    h.pickupArrow = [&] { picked++; return true; };
    ms.spawn(mobs().id("grazer"), {10.5f, 5, 0.5f}, rng);
    ps.shoot(ProjKind::Arrow, {0.5f, 6.0f, 0.5f}, {2.5f, 0.05f, 0}, 2, true);
    for (int i = 0; i < 20; i++) ps.tick(f.w, ms, pl, h);
    CHECK(mobDmg >= 4);                         // fast arrow, high damage
    CHECK(ps.list.empty());
    // arrow into a wall sticks, then is picked up when the player walks over
    f.w.setBlock({0, 6, 6}, blocks().stateOf("stone"));
    ps.shoot(ProjKind::Arrow, {0.5f, 6.5f, 2.0f}, {0, 0, 1.5f}, 2, true);
    for (int i = 0; i < 10; i++) ps.tick(f.w, ms, pl, h);
    REQUIRE(ps.list.size() == 1); CHECK(ps.list[0].stuck);
    CHECK(ps.list[0].pos.z < 6.0f && ps.list[0].pos.z > 5.5f);
    pl.pos = {0.5f, 5, 5.0f}; ps.tick(f.w, ms, pl, h);
    CHECK_EQ(picked, 1); CHECK(ps.list.empty());
    // creature bolt hits the player
    pl.pos = {0.5f, 5, 0.5f};
    ps.shoot(ProjKind::EmberBolt, {0.5f, 6.0f, -8.0f}, {0, 0, 0.9f}, 4, false);
    for (int i = 0; i < 20; i++) ps.tick(f.w, ms, pl, h);
    CHECK_EQ(playerDmg, 4.0f);
}

TEST(cinderwisps_spawn_only_in_the_hollow_and_shoot_with_sight) {
    Flat f("wisp");
    MobSystem ms(mobs()); Rng rng(3);
    Player pl; pl.pos = {0.5f, 5, 0.5f};
    for (int i = 0; i < 300; i++) ms.spawnTick(f.w, pl, 0.0f, rng, 10, 12, "overworld");
    for (auto& m : ms.mobs) CHECK(mobs().def(m.type).name != "cinderwisp");
    ms.mobs.clear();
    int shots = 0;
    MobContext ctx{f.w, pl, true, 0, nullptr, nullptr};
    ctx.shoot = [&](Mob&, Vec3, Vec3 v) { shots++; CHECK(length(v) > 0.5f); };
    ms.spawn(mobs().id("cinderwisp"), {0.5f, 9, 8.5f}, rng);
    for (int i = 0; i < 200; i++) ms.tick(ctx, rng);
    CHECK(shots >= 2);
    CHECK(ms.mobs[0].pos.y > 6);   // still airborne
}
