// Hoppers, droppers, dispensers, comparators and sticky pistons (flat world, ground top y = 5).
#include "testing.h"
#include "game/containers.h"

using namespace vx;
using vxtest::blocks;

namespace {
JobSystem& jobs() { static JobSystem j(2); return j; }
const ItemRegistry& items() { static ItemRegistry r; static bool ok = [] { std::string e; return r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/items.json", blocks(), nullptr, &e); }(); (void)ok; return r; }
const RecipeBook& recipes() { static RecipeBook r; static bool ok = [] { std::string e; return r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/recipes.json", items(), &e); }(); (void)ok; return r; }
struct Env {
    World w{blocks(), jobs(), items().names()};
    std::unique_ptr<BlockBehaviors> b;
    EntityManager em{items()};
    Rng rng{4};
    ContainerContext ctx{w, items(), recipes()};
    explicit Env(const char* n) {
        WorldMeta m; m.gen.preset = "flat"; std::string err; w.create(vxtest::tempDir(n), m, &err);
        for (int z = -2; z <= 2; z++) for (int x = -2; x <= 2; x++) w.loadChunkSync({x, z});
        b = std::make_unique<BlockBehaviors>(w);
        ctx.behaviors = b.get(); ctx.entities = &em; ctx.rng = &rng;
        b->containerSignal = [this](const BlockPos& p) { return containerSignal(ctx, p); };
        b->onDispense = [this](const BlockPos& p, bool d, const BlockPos& dir) { dispense(ctx, p, d, dir); };
    }
    void put(int x, int y, int z, BlockState s) {   // like the game: placing a container creates its block entity
        w.setBlock({x, y, z}, s);
        if (int n = containerSlots(blocks(), s)) { const std::string& nm = blocks().def(s).name; w.ensureBlockEntity({x, y, z}, nm == "hopper" ? BE_HOPPER : nm == "kiln" ? BE_KILN : nm == "chest" ? BE_CHEST : BE_DROPPER)->items.resize(n); }
    }
    BlockState st(const char* n) { return blocks().stateOf(n); }
    BlockState facing(const char* n, int f) { return blocks().withProp(st(n), "facing", f); }
    std::vector<ItemStack>& slots(int x, int y, int z, u8 kind, int n) { auto* be = w.ensureBlockEntity({x, y, z}, kind); be->items.resize(n); return be->items; }
    void run(int t) { for (int i = 0; i < t; i++) { w.tick(); tickContainers(ctx, {0, 0}, 2); } }
    int count(int x, int y, int z) { auto* be = w.blockEntity({x, y, z}); int n = 0; if (be) for (auto& s : be->items) n += s.count; return n; }
};
ItemStack it(const char* n, int c) { return {items().id(n), (u8)c, 0}; }
} // namespace

TEST(hoppers_move_items_chest_to_chest_and_lock_when_powered) {
    Env e("hop1");
    e.put(0, 7, 0, e.st("chest")); e.slots(0, 7, 0, BE_CHEST, CHEST_SLOTS)[3] = it("cobblestone", 10);
    e.put(0, 6, 0, e.facing("hopper", 0));   // pointing down into the chest below
    e.put(0, 5, 0, e.st("chest")); e.slots(0, 5, 0, BE_CHEST, CHEST_SLOTS);
    e.run(200);
    CHECK_EQ(e.count(0, 7, 0), 0);
    CHECK_EQ(e.count(0, 5, 0), 10);
    // locked: a lever powering the hopper stops transfers
    e.slots(0, 7, 0, BE_CHEST, CHEST_SLOTS)[0] = it("dirt", 5);
    e.put(1, 6, 0, e.st("stone")); e.put(1, 7, 0, e.st("lever"));
    e.b->toggleLever({1, 7, 0});   // lever strongly powers the stone beside the hopper
    e.run(100);
    CHECK_EQ(e.count(0, 7, 0), 5);
}

TEST(hoppers_vacuum_items_and_feed_kilns) {
    Env e("hop2");
    e.put(0, 6, 0, e.facing("hopper", 0));
    e.put(0, 5, 0, e.st("kiln"));
    e.em.spawnItem({0.5f, 7.2f, 0.5f}, it("raw_iron", 2), e.rng);
    for (int i = 0; i < 30; i++) { e.em.tick(e.w, Player{}, nullptr); e.run(1); }
    CHECK(e.em.count() == 0);   // sucked in
    e.run(40);
    auto* k = e.w.blockEntity({0, 5, 0});
    REQUIRE(k);
    CHECK_EQ((int)k->items[0].count + (k->items[2].empty() ? 0 : (int)k->items[2].count), 2);   // raw iron reached the kiln input
}

TEST(comparator_reads_container_fullness) {
    Env e("cmp");
    e.put(0, 5, 0, e.st("chest"));
    e.put(0, 5, 1, e.facing("comparator", 1));   // output south (+Z), back = chest at z=0
    e.put(0, 5, 2, e.st("glimmer_wire"));
    e.run(5);
    CHECK_EQ(blocks().getProp(e.w.getBlock({0, 5, 1}), "power"), 0);
    auto& s = e.slots(0, 5, 0, BE_CHEST, CHEST_SLOTS);
    for (int i = 0; i < 14; i++) s[i] = it("dirt", 64);   // ~half full
    e.b->containerChanged({0, 5, 0});
    e.run(5);
    int p = blocks().getProp(e.w.getBlock({0, 5, 1}), "power");
    CHECK(p >= 7 && p <= 9);
    CHECK_EQ(blocks().getProp(e.w.getBlock({0, 5, 2}), "power"), p);   // drives wire at its strength
}

TEST(droppers_and_dispensers_fire_on_rising_edge) {
    Env e("disp");
    e.put(0, 5, 0, e.facing("dropper", 5));   // facing +X
    e.slots(0, 5, 0, BE_DROPPER, DROPPER_SLOTS)[0] = it("sand", 3);
    e.put(-1, 5, 0, e.st("lever"));
    e.b->toggleLever({-1, 5, 0}); e.run(10);
    CHECK_EQ(e.count(0, 5, 0), 2);            // one item out
    CHECK_EQ(e.em.count(), (size_t)1);
    e.run(20); CHECK_EQ(e.count(0, 5, 0), 2);   // held power: no repeat
    e.b->toggleLever({-1, 5, 0}); e.run(5); e.b->toggleLever({-1, 5, 0}); e.run(10);
    CHECK_EQ(e.count(0, 5, 0), 1);            // second pulse
    // dispenser pours water
    e.put(0, 5, 5, e.facing("dispenser", 5));
    e.slots(0, 5, 5, BE_DROPPER, DROPPER_SLOTS)[4] = it("water_bucket", 1);
    e.put(-1, 5, 5, e.st("lever")); e.b->toggleLever({-1, 5, 5}); e.run(10);
    CHECK(blocks().def(e.w.getBlock({1, 5, 5})).name == "water");
    CHECK(e.w.blockEntity({0, 5, 5})->items[4].item == items().id("bucket"));
}

TEST(sticky_piston_pulls_block_back) {
    Env e("sticky");
    e.put(0, 5, 0, e.facing("sticky_piston", 5));
    e.put(1, 5, 0, e.st("bricks"));
    e.put(-1, 5, 0, e.st("lever"));
    e.b->toggleLever({-1, 5, 0}); e.run(5);
    CHECK(blocks().def(e.w.getBlock({2, 5, 0})).name == "bricks");
    e.b->toggleLever({-1, 5, 0}); e.run(5);
    CHECK(blocks().def(e.w.getBlock({1, 5, 0})).name == "bricks");   // pulled back
    CHECK(e.w.getBlock({2, 5, 0}) == AIR);
}

TEST(hoppers_unload_and_load_storage_carts) {
    Env e("hopcart");
    CartSystem carts; e.ctx.carts = &carts;
    // unloading: track over a hopper that feeds a chest below it
    e.put(0, 3, 0, e.st("chest"));
    e.put(0, 4, 0, e.facing("hopper", 0));
    e.w.setBlock({0, 5, 0}, e.st("rail"));
    Cart& full = carts.spawn({0.5f, 5, 0.5f}, CartKind::Storage);
    full.items[0] = it("cobblestone", 3); full.items[4] = it("coal", 2);
    // loading: a hopper above a track pours into the cart beneath it
    e.w.setBlock({4, 5, 0}, e.st("rail"));
    e.put(4, 6, 0, e.facing("hopper", 0));
    e.slots(4, 6, 0, BE_HOPPER, HOPPER_SLOTS)[0] = it("stick", 4);
    Cart& empty = carts.spawn({4.5f, 5, 0.5f}, CartKind::Storage);
    u32 emptyId = empty.id;
    e.run(8 * 12);
    int left = 0; for (auto& s : carts.carts[0].items) left += s.count;
    CHECK_EQ(left, 0);                                   // the cart was emptied ...
    CHECK_EQ(e.count(0, 3, 0) + e.count(0, 4, 0), 5);    // ... into the hopper and chest, nothing lost
    int got = 0; for (auto& s : carts.byId(emptyId)->items) got += s.count;
    CHECK_EQ(got, 4);
    CHECK_EQ(e.count(4, 6, 0), 0);
    // a plain cart is not a container
    Cart& plain = carts.spawn({0.5f, 5, 0.5f});
    (void)plain;
    CHECK(carts.storageAt({0, 5, 0}) == &carts.carts[0]);
    CHECK(carts.storageAt({0, 4, 0}) == nullptr);
}
