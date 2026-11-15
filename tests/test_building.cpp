// Building shapes: per-state boxes, placement rules, fence/pane connections, doors/trapdoors,
// collision (step-up, tall fences) and picking (flat world, surface y = 5).
#include "testing.h"
#include "game/behaviors.h"
#include "game/building.h"
#include "game/player.h"
#include "game/shapes.h"
#include "game/mobs.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
struct Env {
    World w{blocks(), jobs(), vxtest::testItems()};
    std::unique_ptr<BlockBehaviors> b;
    explicit Env(const char* name) {
        std::string err; WorldMeta m; m.gen.preset = "flat"; w.create(vxtest::tempDir(name), m, &err);
        for (int z = -2; z <= 2; z++) for (int x = -2; x <= 2; x++) w.loadChunkSync({x, z});
        b = std::make_unique<BlockBehaviors>(w);
    }
    BlockState st(const char* n) { return blocks().stateOf(n); }
    int prop(const BlockPos& p, const char* n) { return blocks().getProp(w.getBlock(p), n); }
    std::string name(const BlockPos& p) { return blocks().def(w.getBlock(p)).name; }
    bool place(const char* item, BlockPos clicked, BlockPos normal, Vec3 hit, Vec3 look) {
        PlacePlan pl = planBuildingPlacement(w, st(item), clicked, normal, hit, look);
        return applyPlacement(w, pl);
    }
};
const Vec3 LOOK_N{0, -0.3f, -1};   // player looking north (-Z)
} // namespace

TEST(building_shape_tables) {
    const BlockRegistry& r = blocks();
    BlockState slab = r.stateOf("oak_slab");
    REQUIRE(r.boxed(slab));
    auto boxes = r.collisionBoxes(slab);
    REQUIRE(boxes.n == 1); CHECK_EQ((int)boxes.p[0].mx[1], 8);
    CHECK(!r.opaque(slab));
    BlockState dbl = r.withProp(slab, "type", 2);
    CHECK(r.opaque(dbl)); CHECK_EQ((int)r.opacity(dbl), 15);
    BlockState fence = r.stateOf("oak_fence");
    CHECK_EQ(r.renderBoxes(fence).n, 1);
    CHECK_EQ((int)r.collisionBoxes(fence).p[0].mx[1], 24);   // 1.5 blocks tall: cannot be jumped
    BlockState f2 = r.withProp(r.withProp(fence, "north", 1), "east", 1);
    CHECK_EQ(r.renderBoxes(f2).n, 5);
    BlockState stairs = r.withProp(r.stateOf("oak_stairs"), "facing", 0);
    AABB u = blockShapeBounds(r, stairs);
    CHECK(u.min.y == 0 && u.max.y == 1 && u.max.z == 1);
    CHECK(!r.boxed(r.stateOf("stone")));
}

TEST(building_slabs_stack_into_double_and_choose_halves) {
    Env e("bslab");
    // on the ground (top face of y=4): bottom slab at y=5
    REQUIRE(e.place("oak_slab", {0, 4, 0}, {0, 1, 0}, {0.5f, 5.0f, 0.5f}, LOOK_N));
    CHECK_EQ(e.prop({0, 5, 0}, "type"), 0);
    // clicking its top again merges into a double slab in place
    REQUIRE(e.place("oak_slab", {0, 5, 0}, {0, 1, 0}, {0.5f, 5.5f, 0.5f}, LOOK_N));
    CHECK_EQ(e.prop({0, 5, 0}, "type"), 2);
    CHECK(e.name({0, 6, 0}) == "air");
    // against the upper half of a wall: top slab
    e.w.setBlock({2, 5, 0}, e.st("stone"));
    REQUIRE(e.place("stone_slab", {2, 5, 0}, {0, 0, 1}, {2.5f, 5.8f, 1.0f}, LOOK_N));
    CHECK_EQ(e.prop({2, 5, 1}, "type"), 1);
    // a different slab type does not merge
    REQUIRE(e.place("cobblestone_slab", {0, 5, 0}, {0, 1, 0}, {0.5f, 6.0f, 0.5f}, LOOK_N));
    CHECK(e.name({0, 6, 0}) == "cobblestone_slab");
}

TEST(building_stairs_face_away_from_player) {
    Env e("bstairs");
    REQUIRE(e.place("oak_stairs", {0, 4, 0}, {0, 1, 0}, {0.5f, 5.0f, 0.5f}, LOOK_N));
    CHECK_EQ(e.prop({0, 5, 0}, "facing"), 0);   // tall back to the north: walk up going north
    CHECK_EQ(e.prop({0, 5, 0}, "half"), 0);
}

TEST(building_fences_connect_and_disconnect) {
    Env e("bfence");
    REQUIRE(e.place("oak_fence", {0, 4, 0}, {0, 1, 0}, {0.5f, 5, 0.5f}, LOOK_N));
    CHECK_EQ(e.prop({0, 5, 0}, "east"), 0);
    REQUIRE(e.place("oak_fence", {1, 4, 0}, {0, 1, 0}, {1.5f, 5, 0.5f}, LOOK_N));
    CHECK_EQ(e.prop({0, 5, 0}, "east"), 1);     // neighbour update connected the first fence
    CHECK_EQ(e.prop({1, 5, 0}, "west"), 1);     // placement connected the new one
    e.w.setBlock({0, 5, 1}, e.st("stone"));     // a solid wall to the south
    CHECK_EQ(e.prop({0, 5, 0}, "south"), 1);
    e.w.setBlock({1, 5, 0}, AIR);
    CHECK_EQ(e.prop({0, 5, 0}, "east"), 0);
    // panes join glass and other panes
    e.w.setBlock({4, 5, 0}, e.st("glass"));
    REQUIRE(e.place("glass_pane", {5, 4, 0}, {0, 1, 0}, {5.5f, 5, 0.5f}, LOOK_N));
    CHECK_EQ(e.prop({5, 5, 0}, "west"), 1);
}

TEST(building_doors_place_toggle_break_and_power) {
    Env e("bdoor");
    REQUIRE(e.place("oak_door", {0, 4, 0}, {0, 1, 0}, {0.5f, 5, 0.5f}, LOOK_N));
    REQUIRE(e.name({0, 5, 0}) == "oak_door" && e.name({0, 6, 0}) == "oak_door");
    CHECK_EQ(e.prop({0, 5, 0}, "upper"), 0); CHECK_EQ(e.prop({0, 6, 0}, "upper"), 1);
    CHECK(toggleOpenable(e.w, {0, 6, 0}));      // clicking the upper half opens both
    CHECK_EQ(e.prop({0, 5, 0}, "open"), 1); CHECK_EQ(e.prop({0, 6, 0}, "open"), 1);
    CHECK(toggleOpenable(e.w, {0, 5, 0}));
    CHECK_EQ(e.prop({0, 6, 0}, "open"), 0);
    // second door to the clockwise side mirrors its hinge: a double door
    int dx, dz; dirOffset(dirCCW(e.prop({0, 5, 0}, "facing")), dx, dz);
    REQUIRE(e.place("oak_door", {dx, 4, dz}, {0, 1, 0}, {dx + 0.5f, 5, dz + 0.5f}, LOOK_N));
    CHECK_EQ(e.prop({dx, 5, dz}, "hinge"), 1);
    // breaking the lower half removes the upper one
    e.w.setBlock({0, 5, 0}, AIR);
    CHECK(e.name({0, 6, 0}) == "air");
    // iron door: not by hand, but a lever next to it opens it
    REQUIRE(e.place("iron_door", {4, 4, 0}, {0, 1, 0}, {4.5f, 5, 0.5f}, LOOK_N));
    CHECK(!toggleOpenable(e.w, {4, 5, 0}));
    e.w.setBlock({5, 5, 0}, e.st("lever"));
    e.b->toggleLever({5, 5, 0});
    for (int i = 0; i < 4; i++) e.w.tick();
    CHECK_EQ(e.prop({4, 5, 0}, "open"), 1); CHECK_EQ(e.prop({4, 6, 0}, "open"), 1);
    e.b->toggleLever({5, 5, 0});
    for (int i = 0; i < 4; i++) e.w.tick();
    CHECK_EQ(e.prop({4, 5, 0}, "open"), 0);
}

TEST(building_collision_step_up_and_tall_fences) {
    Env e("bcoll");
    // a slab step and then a full block: the player walks onto the slab without jumping
    for (int x = 2; x <= 9; x++) e.w.setBlock({x, 5, 0}, e.st("oak_slab"));
    Player p; p.flying = false; p.pos = {0.5f, 5, 0.5f}; p.yaw = PI / 2;   // facing +X
    PlayerInput in; in.forward = 1;
    for (int i = 0; i < 40; i++) p.tick(e.w, in);
    CHECK(p.pos.x > 2.5f);
    CHECK(std::fabs(p.pos.y - 5.5f) < 1e-3f);   // standing on the slab
    // a fence cannot be stepped onto or jumped over
    Player q; q.flying = false; q.pos = {0.5f, 5, 4.5f}; q.yaw = PI / 2;
    e.w.setBlock({2, 5, 4}, e.st("oak_fence"));
    PlayerInput j; j.forward = 1; j.jump = true;
    for (int i = 0; i < 60; i++) q.tick(e.w, j);
    CHECK(q.pos.x < 2.4f);
    // a full block is not climbed by step-up either
    Player r; r.flying = false; r.pos = {0.5f, 5, 8.5f}; r.yaw = PI / 2;
    e.w.setBlock({2, 5, 8}, e.st("stone"));
    for (int i = 0; i < 40; i++) r.tick(e.w, in);
    CHECK(r.pos.x < 2.0f); CHECK(std::fabs(r.pos.y - 5) < 1e-3f);
}

TEST(building_ray_passes_open_door_gap_and_hits_slab_top) {
    Env e("bray");
    e.w.setBlock({0, 5, 0}, e.st("oak_slab"));
    RayHit h = raycastBlocks(e.w, {0.5f, 7, 0.5f}, {0, -1, 0}, 5);
    REQUIRE(h.hit); CHECK((h.block == BlockPos{0, 5, 0})); CHECK(std::fabs(h.point.y - 5.5f) < 1e-3f); CHECK((h.normal == BlockPos{0, 1, 0}));
    // a door panel on the north side: a ray through the south part of the cell misses it
    BlockState d = blocks().withProp(e.st("oak_door"), "facing", 0);
    PlacePlan pl; pl.ok = true; pl.at = {3, 5, 0}; pl.state = d; pl.upper = blocks().withProp(d, "upper", 1);
    REQUIRE(applyPlacement(e.w, pl));
    RayHit m = raycastBlocks(e.w, {3.5f, 5.5f, 0.8f + 2}, {0, 0, -1}, 1.5f);   // stops before reaching the panel
    CHECK(!m.hit);
    RayHit k = raycastBlocks(e.w, {3.5f, 5.5f, 3}, {0, 0, -1}, 5);
    REQUIRE(k.hit); CHECK((k.block == BlockPos{3, 5, 0})); CHECK(std::fabs(k.point.z - 3.0f / 16) < 1e-3f);
}

TEST(building_pathfinding_doorways_and_fences) {
    Env e("bpath");
    // a wall along x = 3 with a closed door in it; a path across must wait for the door to open
    // a closed 3x3 room around (6, 5, 0) with roof; its only way in is a door at (5, 5, 0)
    for (int x = 5; x <= 7; x++) for (int z = -1; z <= 1; z++) {
        for (int y = 5; y <= 6; y++) if (!(x == 6 && z == 0)) e.w.setBlock({x, y, z}, e.st("stone"));
        e.w.setBlock({x, 7, z}, e.st("stone"));
    }
    e.w.setBlock({5, 5, 0}, AIR); e.w.setBlock({5, 6, 0}, AIR);
    BlockState d = blocks().withProp(e.st("oak_door"), "facing", 2);
    PlacePlan pl; pl.ok = true; pl.at = {5, 5, 0}; pl.state = d; pl.upper = blocks().withProp(d, "upper", 1);
    REQUIRE(applyPlacement(e.w, pl));
    CHECK(findPath(e.w, {0, 5, 0}, {6, 5, 0}, 2, 4000).empty());
    REQUIRE(toggleOpenable(e.w, {5, 5, 0}));
    CHECK(!findPath(e.w, {0, 5, 0}, {6, 5, 0}, 2, 4000).empty());
    // nobody stands on a fence
    e.w.setBlock({0, 5, 4}, e.st("oak_fence"));
    CHECK(!walkableCell(e.w, {0, 6, 4}, 2));
}
