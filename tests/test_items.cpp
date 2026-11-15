// Items, inventory, crafting, survival rules and dropped-item entities.
#include "testing.h"
#include "game/entities.h"
#include "game/survival.h"
#include "game/kiln.h"

using namespace vx;
using vxtest::blocks;

namespace {
const ItemRegistry& items() {
    static ItemRegistry r; static bool ok = [] { std::string e; bool b = r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/items.json", blocks(), nullptr, &e); if (!b) fprintf(stderr, "%s\n", e.c_str()); return b; }();
    (void)ok; return r;
}
const RecipeBook& recipes() {
    static RecipeBook r; static bool ok = [] { std::string e; bool b = r.loadFile(std::string(VX_SOURCE_DIR) + "/assets/data/recipes.json", items(), &e); if (!b) fprintf(stderr, "%s\n", e.c_str()); return b; }();
    (void)ok; return r;
}
ItemStack st(const char* n, int c = 1) { return {items().id(n), (u8)c, 0}; }
JobSystem& jobs() { static JobSystem j(2); return j; }
} // namespace

TEST(items_registry_has_block_and_tool_items) {
    REQUIRE(items().size() > 50);
    CHECK(items().id("stone") != 0);
    CHECK(items().id("water") == 0);          // fluids have no item
    CHECK(items().def(items().id("stone")).places == blocks().stateOf("stone"));
    const ItemDef& pk = items().def(items().id("iron_pickaxe"));
    CHECK_EQ(pk.maxStack, 1); CHECK_EQ(pk.tier, 2); CHECK(pk.tool == ToolType::Pickaxe);
    CHECK(items().def(items().id("glimmer_dust")).places == blocks().stateOf("glimmer_wire"));
    CHECK(items().def(items().id("apple")).isFood());
}

TEST(items_inventory_stacking_and_overflow) {
    Inventory inv(4);
    CHECK_EQ(inv.add(st("dirt", 40), items()), 0);
    CHECK_EQ(inv.add(st("dirt", 40), items()), 0);    // 64 + 16
    CHECK_EQ((int)inv.slots[0].count, 64); CHECK_EQ((int)inv.slots[1].count, 16);
    CHECK_EQ(inv.add(st("iron_pickaxe"), items()), 0);
    CHECK_EQ(inv.add(st("iron_pickaxe"), items()), 0);  // tools never stack
    CHECK_EQ(inv.add(st("stone", 5), items()), 5);      // full
    CHECK(inv.remove(items().id("dirt"), 70));
    CHECK_EQ(inv.count(items().id("dirt")), 10);
    CHECK(!inv.remove(items().id("dirt"), 11));
}

TEST(items_click_slot_semantics) {
    ItemStack slot = st("sand", 10), held{};
    clickSlot(slot, held, true, items());              // right click: take half
    CHECK_EQ((int)held.count, 5); CHECK_EQ((int)slot.count, 5);
    clickSlot(slot, held, false, items());             // left: merge all
    CHECK_EQ((int)slot.count, 10); CHECK(held.empty());
    ItemStack empty{}; held = st("gravel", 3);
    clickSlot(empty, held, true, items());             // right on empty: place one
    CHECK_EQ((int)empty.count, 1); CHECK_EQ((int)held.count, 2);
    clickSlot(slot, held, false, items());             // different items: swap
    CHECK(slot.item == items().id("gravel")); CHECK(held.item == items().id("sand"));
}

TEST(items_crafting_shaped_tags_mirror) {
    REQUIRE(recipes().shapedCount() > 20);
    ItemStack g[9] = {};
    g[4] = st("birch_log");
    auto r = recipes().match(g, 3, 3);
    REQUIRE(r); CHECK(r->item == items().id("birch_planks")); CHECK_EQ((int)r->count, 4);
    // wooden pickaxe from mixed planks
    ItemStack p[9] = {st("oak_planks"), st("pine_planks"), st("birch_planks"), {}, st("stick"), {}, {}, st("stick"), {}};
    r = recipes().match(p, 3, 3);
    REQUIRE(r); CHECK(r->item == items().id("wooden_pickaxe"));
    // axe mirrored
    ItemStack a[9] = {{}, st("iron_ingot"), st("iron_ingot"), {}, st("stick"), st("iron_ingot"), {}, st("stick"), {}};
    r = recipes().match(a, 3, 3);
    REQUIRE(r); CHECK(r->item == items().id("iron_axe"));
    // 2x2 grid: workbench, sticks; consumption
    ItemStack s[4] = {st("oak_planks", 3), st("oak_planks"), st("oak_planks"), st("oak_planks")};
    r = recipes().match(s, 2, 2);
    REQUIRE(r); CHECK(r->item == items().id("workbench"));
    RecipeBook::consume(s, 4);
    CHECK_EQ((int)s[0].count, 2); CHECK(s[1].empty());
    ItemStack bad[4] = {st("dirt"), st("stick"), {}, {}};
    CHECK(!recipes().match(bad, 2, 2));
    CHECK(recipes().smelt(items().id("raw_iron")) == items().id("iron_ingot"));
    CHECK(recipes().smelt(items().id("pine_log")) == items().id("coal"));
}

TEST(survival_harvest_and_break_time) {
    const BlockDef& stone = blocks().def(blocks().stateOf("stone"));
    const ItemDef& wp = items().def(items().id("wooden_pickaxe"));
    const ItemDef& sp = items().def(items().id("stone_pickaxe"));
    CHECK(!canHarvest(stone, nullptr));
    CHECK(canHarvest(stone, &wp));
    CHECK(!canHarvest(blocks().def(blocks().stateOf("iron_ore")), &wp));
    CHECK(canHarvest(blocks().def(blocks().stateOf("iron_ore")), &sp));
    CHECK(breakSeconds(stone, &sp, true, false) < breakSeconds(stone, &wp, true, false));
    CHECK(breakSeconds(stone, nullptr, true, false) > breakSeconds(stone, &wp, true, false) * 3);
    CHECK(breakSeconds(blocks().def(blocks().stateOf("bedrock")), &sp, true, false) < 0);
    CHECK_EQ(breakSeconds(blocks().def(blocks().stateOf("torch")), nullptr, true, false), 0.0f);
    Rng rng(1);
    CHECK(blockDrops(blocks().stateOf("stone"), blocks(), items(), recipes(), nullptr, rng).empty());
    auto d = blockDrops(blocks().stateOf("stone"), blocks(), items(), recipes(), &wp, rng);
    REQUIRE(d.size() == 1); CHECK(d[0].item == items().id("cobblestone"));
    d = blockDrops(blocks().stateOf("oak_planks"), blocks(), items(), recipes(), nullptr, rng);
    REQUIRE(d.size() == 1); CHECK(d[0].item == items().id("oak_planks"));
    int saplings = 0; for (int i = 0; i < 2000; i++) for (auto& s : blockDrops(blocks().stateOf("oak_leaves"), blocks(), items(), recipes(), nullptr, rng)) if (s.item == items().id("sapling")) saplings++;
    CHECK(saplings > 50 && saplings < 160);   // ~5%
}

TEST(survival_stats_damage_regen_drown_starve) {
    PlayerStats s;
    CHECK_EQ(fallDamage(3.0f), 0.0f); CHECK_EQ(fallDamage(10.0f), 7.0f);
    CHECK_EQ(s.damage(5, DamageSource::Fall), 5.0f);
    CHECK_EQ(s.damage(5, DamageSource::Fall), 0.0f);   // invulnerability window
    for (int i = 0; i < 10; i++) s.tick(false, false, false);
    for (int i = 0; i < 400; i++) s.tick(false, false, false);
    CHECK(s.health > 15);                               // regenerated while fed
    PlayerStats w;
    for (int i = 0; i < 300 + 200; i++) w.tick(true, false, false);
    CHECK(w.air == 0); CHECK(w.health < 20);
    PlayerStats h; h.food = 0; h.saturation = 0;
    for (int i = 0; i < 800; i++) h.tick(false, false, false);
    CHECK(h.health <= 10);
    PlayerStats c; c.food = 2; c.eat(items().def(items().id("apple"))); CHECK_EQ(c.food, 6.0f);
    ByteWriter bw; bw.bytes(h.encode().data(), h.encode().size()); ByteReader br(bw.buf); PlayerStats h2; CHECK(h2.decode(br)); CHECK_EQ(h2.health, h.health);
}

TEST(entities_items_fall_get_picked_up_and_persist) {
    auto dir = vxtest::tempDir("ent");
    ItemNames names = items().names();
    {
        EntityManager em(items());   // must outlive the World: close() saves entities through it
        World w(blocks(), jobs(), names);
        w.setEntityHost(&em);
        WorldMeta m; m.gen.preset = "flat"; std::string err; REQUIRE(w.create(dir, m, &err));
        for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) w.loadChunkSync({x, z});
        Rng rng(3);
        em.spawnItem({4.5f, 12, 4.5f}, st("coal", 3), rng);
        em.spawnItem({4.6f, 12, 4.4f}, st("coal", 2), rng);
        Player far; far.pos = {100, 5, 100};
        for (int i = 0; i < 100; i++) em.tick(w, far, nullptr);
        REQUIRE(em.count() == 1);                         // merged
        CHECK(std::fabs(em.items[0].pos.y - 5.0f) < 0.01f);   // resting on the turf (top at y=5)
        CHECK_EQ((int)em.items[0].stack.count, 5);
        REQUIRE(w.save(true, {}, &err));
        CHECK(w.storage().hasRecord({0, 0}));            // entity makes the chunk persistent
        w.close();   // final save still collects entities from the host
    }
    EntityManager em(items());
    World w(blocks(), jobs(), names);
    w.setEntityHost(&em);
    std::string err; REQUIRE(w.open(dir, &err));
    for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) w.loadChunkSync({x, z});
    REQUIRE(em.count() == 1);
    CHECK_EQ((int)em.items[0].stack.count, 5);
    Inventory inv; Player p; p.pos = {4.5f, 5, 4.5f};
    em.tick(w, p, &inv);
    CHECK_EQ(inv.count(items().id("coal")), 5);
    CHECK_EQ(em.count(), (size_t)0);
    REQUIRE(w.save(true, {}, &err));
    CHECK(!w.storage().hasRecord({0, 0}));               // nothing persistent left: record removed
}

TEST(kiln_smelts_with_fuel) {
    BlockEntity be; be.kind = BE_KILN; be.items.resize(KILN_SLOTS);
    be.items[0] = st("raw_iron", 3); be.items[1] = st("coal", 1);
    bool lit = false; int ticks = 0;
    while (ticks < 2000 && (be.items[2].count < 3)) { tickKiln(be, items(), recipes(), lit); ticks++; }
    CHECK_EQ((int)be.items[2].count, 3);
    CHECK(be.items[2].item == items().id("iron_ingot"));
    CHECK(be.items[0].empty()); CHECK(be.items[1].empty());
    CHECK(ticks >= 3 * KILN_COOK_TICKS && ticks <= 3 * KILN_COOK_TICKS + 5);
    // no fuel: nothing happens
    BlockEntity b2; b2.items.resize(KILN_SLOTS); b2.items[0] = st("sand", 1);
    for (int i = 0; i < 300; i++) tickKiln(b2, items(), recipes(), lit);
    CHECK(b2.items[2].empty()); CHECK(!lit);
}

TEST(structure_loot_is_deterministic_per_seed_and_position) {
    std::vector<ItemStack> a(CHEST_SLOTS), b(CHEST_SLOTS), c(CHEST_SLOTS);
    recipes().rollStructureLoot(77, {10, 40, -3}, a);
    recipes().rollStructureLoot(77, {10, 40, -3}, b);
    recipes().rollStructureLoot(77, {11, 40, -3}, c);
    CHECK(a == b);
    int n = 0; for (auto& s : a) n += !s.empty();
    CHECK(n > 0);
    CHECK(!(a == c));   // different chest, different contents
}

#include "game/behaviors.h"
TEST(farming_grain_grows_on_moist_farmland_and_drops_by_age) {
    static JobSystem jobs(2);
    World w(blocks(), jobs, items().names());
    WorldMeta m; m.gen.preset = "flat"; std::string err; REQUIRE(w.create(vxtest::tempDir("farm"), m, &err));
    for (int z = -1; z <= 1; z++) for (int x = -1; x <= 1; x++) w.loadChunkSync({x, z});
    BlockBehaviors b(w);
    w.setBlock({2, 4, 2}, blocks().stateOf("farmland"));
    w.setBlock({2, 5, 2}, blocks().stateOf("grain"));
    w.setBlock({4, 4, 2}, blocks().stateOf("water"));
    for (int i = 0; i < 3000 && blocks().getProp(w.getBlock({2, 5, 2}), "age") < 7; i++) {
        w.onRandomTick({2, 4, 2}, w.getBlock({2, 4, 2}));
        w.onRandomTick({2, 5, 2}, w.getBlock({2, 5, 2}));
    }
    CHECK_EQ(blocks().getProp(w.getBlock({2, 4, 2}), "moist"), 1);
    CHECK_EQ(blocks().getProp(w.getBlock({2, 5, 2}), "age"), 7);
    Rng rng(1);
    auto ripe = blockDrops(w.getBlock({2, 5, 2}), blocks(), items(), recipes(), nullptr, rng);
    REQUIRE(ripe.size() == 2); CHECK(ripe[0].item == items().id("grain"));
    auto young = blockDrops(blocks().stateOf("grain"), blocks(), items(), recipes(), nullptr, rng);
    REQUIRE(young.size() == 1); CHECK(young[0].item == items().id("grain_seeds"));
    CHECK_EQ(items().def(items().id("iron_chestplate")).armorSlot, 1);
    CHECK_EQ(items().def(items().id("iron_chestplate")).armorPoints, 6);
    ItemStack g[9] = {}; g[3] = st("grain"); g[4] = st("grain"); g[5] = st("grain");
    auto bread = recipes().match(g, 3, 3); REQUIRE(bread); CHECK(bread->item == items().id("bread"));
}
