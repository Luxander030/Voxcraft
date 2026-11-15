// Player movement, collision and raycast tests on a flat world (surface at y = 5).
#include "testing.h"
#include "game/player.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
struct FlatWorld {
    World w{blocks(), jobs(), vxtest::testItems()};
    FlatWorld(const char* name) {
        WorldMeta m; m.gen.preset = "flat"; std::string err;
        w.create(vxtest::tempDir(name), m, &err);
        for (int z = -2; z <= 2; z++) for (int x = -2; x <= 2; x++) w.loadChunkSync({x, z});
    }
};
void run(Player& p, const World& w, PlayerInput in, int ticks) { for (int i = 0; i < ticks; i++) p.tick(w, in); }
} // namespace

TEST(game_player_falls_and_lands_on_surface) {
    FlatWorld f("p_fall");
    Player p; p.flying = false; p.pos = {0.5f, 20, 0.5f};
    run(p, f.w, {}, 100);
    CHECK(p.onGround);
    CHECK(std::fabs(p.pos.y - 5.0f) < 0.01f);
    CHECK(p.fallDistance == 0);
}

TEST(game_player_reports_fall_distance_on_landing) {
    FlatWorld f("p_falldist");
    Player p; p.pos = {0.5f, 15, 0.5f};
    float landed = -1;
    for (int i = 0; i < 100; i++) { p.tick(f.w, {}); if (p.landedFall >= 0) landed = p.landedFall; }
    CHECK(landed > 9.9f && landed < 10.1f);
}

TEST(game_player_walls_block_movement) {
    FlatWorld f("p_wall");
    for (int y = 5; y < 8; y++) for (int z = -3; z <= 3; z++) f.w.setBlock({3, y, z}, blocks().stateOf("stone"));
    Player p; p.pos = {0.5f, 5, 0.5f}; p.yaw = PI / 2;   // facing +X
    PlayerInput in; in.forward = 1;
    run(p, f.w, in, 60);
    CHECK(p.pos.x <= 3.0f - 0.3f + 1e-3f);
    CHECK(p.pos.x > 2.6f);   // walked up to the wall
}

TEST(game_player_jumps_one_block_not_two) {
    FlatWorld f("p_jump");
    BlockState st = blocks().stateOf("stone");
    for (int z = -3; z <= 3; z++) f.w.setBlock({2, 5, z}, st);                                   // 1-high step at x=2
    for (int z = -3; z <= 3; z++) { f.w.setBlock({6, 6, z}, st); f.w.setBlock({6, 7, z}, st); }  // 2-high wall at x=6 (on the step level)
    for (int x = 3; x <= 6; x++) for (int z = -3; z <= 3; z++) f.w.setBlock({x, 5, z}, st);
    Player p; p.pos = {0.5f, 5, 0.5f}; p.yaw = PI / 2;
    PlayerInput in; in.forward = 1; in.jump = true;
    run(p, f.w, in, 120);
    CHECK(p.pos.y >= 6.0f - 1e-3f);          // climbed the 1-block step
    CHECK(p.pos.x < 6.0f);                   // but not the 2-block wall
}

TEST(game_sneak_does_not_walk_off_edges) {
    FlatWorld f("p_sneak");
    BlockState st = blocks().stateOf("stone");
    for (int x = -2; x <= 1; x++) for (int z = -2; z <= 2; z++) f.w.setBlock({x, 5, z}, st);   // platform ends at x = 2
    Player p; p.pos = {0.5f, 6, 0.5f}; p.yaw = PI / 2;
    run(p, f.w, {}, 10);
    PlayerInput in; in.forward = 1; in.sneak = true;
    run(p, f.w, in, 200);
    CHECK(p.pos.y >= 6.0f - 1e-3f);
    CHECK(p.pos.x < 2.0f + 0.3f);
}

TEST(game_raycast_hits_face) {
    FlatWorld f("p_ray");
    f.w.setBlock({0, 7, -3}, blocks().stateOf("stone"));
    RayHit h = raycastBlocks(f.w, {0.5f, 7.5f, 0.5f}, {0, 0, -1}, 5);
    REQUIRE(h.hit);
    CHECK_EQ(h.block.x, 0); CHECK_EQ(h.block.y, 7); CHECK_EQ(h.block.z, -3);
    CHECK_EQ(h.normal.z, 1);
    CHECK(std::fabs(h.distance - 2.5f) < 1e-3f);
    RayHit down = raycastBlocks(f.w, {0.5f, 7.5f, 0.5f}, {0, -1, 0}, 5);
    REQUIRE(down.hit); CHECK_EQ(down.block.y, 4); CHECK_EQ(down.normal.y, 1);
    RayHit none = raycastBlocks(f.w, {0.5f, 7.5f, 0.5f}, {0, 1, 0}, 5);
    CHECK(!none.hit);
}

TEST(game_fly_toggle_and_landing) {
    FlatWorld f("p_fly");
    Player p; p.pos = {0.5f, 5, 0.5f};
    PlayerInput t; t.toggleFly = true; p.tick(f.w, t);
    CHECK(p.flying);
    PlayerInput up; up.jump = true; run(p, f.w, up, 20);
    CHECK(p.pos.y > 8);
    PlayerInput down; down.sneak = true; run(p, f.w, down, 60);
    CHECK(!p.flying);   // touching ground ends flight
}
