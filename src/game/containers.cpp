#include "game/containers.h"

namespace vx {

void rollGeneratedChestLoot(World& w, const RecipeBook& rb, const BlockPos& p, BlockEntity& be) {
    ChunkPtr ch = w.chunk(p.chunk());
    if (ch && !ch->delta.count(columnIndex(p.x & 15, p.y, p.z & 15))) rb.rollStructureLoot(w.meta.gen.seed, p, be.items);   // part of the generated terrain
}

namespace {
const BlockPos HOPPER_DIR[5] = {{0, -1, 0}, {0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}};   // facing: down N S W E

BlockEntity* containerAt(ContainerContext& c, const BlockPos& p, BlockState* out = nullptr) {
    BlockState s = c.world.getBlock(p);
    int n = containerSlots(c.world.reg, s);
    if (!n) return nullptr;
    const BlockDef& d = c.world.reg.def(s);
    u8 kind = d.has(BF_INTERACT_KILN) ? BE_KILN : d.has(BF_INTERACT_CHEST) ? BE_CHEST : d.name == "hopper" ? BE_HOPPER : BE_DROPPER;
    bool fresh = c.world.blockEntity(p) == nullptr;
    BlockEntity* be = c.world.ensureBlockEntity(p, kind);
    if (be && (int)be->items.size() < n) be->items.resize(n);
    if (be && fresh && kind == BE_CHEST) rollGeneratedChestLoot(c.world, c.recipes, p, *be);
    if (out) *out = s;
    return be;
}

// Moves one item from `from` slot list into container `to` (entering from `face`: 0 top, 1 side, 2 bottom).
bool insertOne(ContainerContext& c, BlockEntity& to, BlockState toState, ItemStack& src, int face) {
    if (src.empty()) return false;
    const BlockDef& d = c.world.reg.def(toState);
    auto tryslot = [&](ItemStack& t) {
        if (t.empty()) { t = src; t.count = 1; }
        else if (canMerge(t, src, c.items) && t.count < c.items.maxStack(t.item)) t.count++;
        else return false;
        src.count--; if (!src.count) src = {};
        return true;
    };
    if (d.has(BF_INTERACT_KILN)) {   // top -> input, sides -> fuel (only fuels), never into the output
        if (face == 0) return c.recipes.smelt(src.item) ? tryslot(to.items[0]) : false;
        return c.items.def(src.item).fuel > 0 ? tryslot(to.items[1]) : false;
    }
    for (auto& t : to.items) if (!t.empty() && canMerge(t, src, c.items) && t.count < c.items.maxStack(t.item)) return tryslot(t);
    for (auto& t : to.items) if (t.empty()) return tryslot(t);
    return false;
}

// Moves one item into a plain slot list (storage carts): merge first, then the first empty slot.
bool insertInto(ContainerContext& c, std::vector<ItemStack>& to, const ItemStack& one) {
    for (auto& t : to) if (!t.empty() && canMerge(t, one, c.items) && t.count < c.items.maxStack(t.item)) { t.count++; return true; }
    for (auto& t : to) if (t.empty()) { t = one; t.count = 1; return true; }
    return false;
}

// Index of the slot a hopper below may pull from (kiln: output only).
int pullSlot(ContainerContext& c, const BlockEntity& be, BlockState s) {
    if (c.world.reg.def(s).has(BF_INTERACT_KILN)) return be.items.size() > 2 && !be.items[2].empty() ? 2 : -1;
    for (size_t i = 0; i < be.items.size(); i++) if (!be.items[i].empty()) return (int)i;
    return -1;
}
} // namespace

int containerSlots(const BlockRegistry& reg, BlockState s) {
    const BlockDef& d = reg.def(s);
    if (d.has(BF_INTERACT_CHEST)) return CHEST_SLOTS;
    if (d.has(BF_INTERACT_KILN)) return KILN_SLOTS;
    if (d.name == "hopper") return HOPPER_SLOTS;
    if (d.name == "dropper" || d.name == "dispenser") return DROPPER_SLOTS;
    return 0;
}

int containerSignal(ContainerContext& c, const BlockPos& p) {
    BlockState s = c.world.getBlock(p);
    if (!containerSlots(c.world.reg, s)) return -1;
    const BlockEntity* be = c.world.blockEntity(p);
    if (!be || be->items.empty()) return 0;
    float fill = 0; bool any = false;
    for (auto& it : be->items) if (!it.empty()) { fill += (float)it.count / (float)c.items.maxStack(it.item); any = true; }
    if (!any) return 0;
    return 1 + (int)std::floor(14.0f * fill / (float)be->items.size());
}

void tickContainers(ContainerContext& c, ChunkPos center, int radius) {
    struct Job { BlockPos p; BlockEntity* be; Chunk* ch; };
    std::vector<Job> hoppers, kilns;
    c.world.forEachChunk([&](ChunkPos cp, Chunk& ch, ChunkState) {
        if (std::abs(cp.x - center.x) > radius || std::abs(cp.z - center.z) > radius) return;
        for (auto& be : ch.blockEntities) {
            int lx, y, lz; columnIndexDecode(be.index, lx, y, lz);
            BlockPos p{cp.x * 16 + lx, y, cp.z * 16 + lz};
            if (be.kind == BE_KILN) kilns.push_back({p, &be, &ch});
            else if (be.kind == BE_HOPPER) hoppers.push_back({p, &be, &ch});
        }
    });
    // kilns
    std::vector<std::pair<BlockPos, bool>> litChanges;
    for (auto& k : kilns) {
        bool lit = false;
        if (tickKiln(*k.be, c.items, c.recipes, lit)) { k.ch->unsaved = true; if (c.behaviors) c.behaviors->containerChanged(k.p); }
        BlockState s = c.world.getBlock(k.p);
        if (c.world.reg.def(s).propIndex("lit") >= 0 && (c.world.reg.getProp(s, "lit") != 0) != lit) litChanges.push_back({k.p, lit});
    }
    for (auto& [p, lit] : litChanges) c.world.setBlock(p, c.world.reg.withProp(c.world.getBlock(p), "lit", lit ? 1 : 0));
    // hoppers (deterministic order: by position)
    std::sort(hoppers.begin(), hoppers.end(), [](const Job& a, const Job& b) { return a.p.y != b.p.y ? a.p.y < b.p.y : a.p.z != b.p.z ? a.p.z < b.p.z : a.p.x < b.p.x; });
    for (auto& h : hoppers) {
        BlockEntity* be = c.world.blockEntity(h.p);   // re-fetch: earlier transfers may have created entities
        if (!be) continue;
        if (be->items.size() < HOPPER_SLOTS) be->items.resize(HOPPER_SLOTS);
        if (be->data[0] > 0) { be->data[0]--; continue; }
        if (c.behaviors && c.behaviors->inputPower(h.p) > 0) continue;   // locked by a signal
        bool moved = false;
        BlockState hs = c.world.getBlock(h.p);
        // push one item in the facing direction
        BlockPos dir = HOPPER_DIR[clamp(c.world.reg.getProp(hs, "facing"), 0, 4)], tp = h.p + dir;
        BlockState ts; BlockEntity* target = containerAt(c, tp, &ts);
        if (target) {
            be = c.world.blockEntity(h.p);
            for (auto& slot : be->items) {
                if (slot.empty()) continue;
                ItemStack one = slot; one.count = 1;
                if (insertOne(c, *target, ts, one, dir.y < 0 ? 0 : 1)) { slot.count--; if (!slot.count) slot = {}; moved = true; if (c.behaviors) c.behaviors->containerChanged(tp); break; }
            }
        } else if (Cart* cart = c.carts ? c.carts->inventoryAt(tp) : nullptr) {   // load a storage cart
            be = c.world.blockEntity(h.p);
            for (auto& slot : be->items) {
                if (slot.empty()) continue;
                if (insertInto(c, cart->items, slot)) { slot.count--; if (!slot.count) slot = {}; moved = true; break; }
            }
        }
        // pull one item from the container above, or vacuum dropped items
        BlockPos up{h.p.x, h.p.y + 1, h.p.z};
        BlockState us; BlockEntity* source = containerAt(c, up, &us);
        be = c.world.blockEntity(h.p);
        if (source) {
            int k = pullSlot(c, *source, us);
            if (k >= 0) {
                ItemStack one = source->items[k]; one.count = 1;
                if (insertOne(c, *be, hs, one, 0)) { source->items[k].count--; if (!source->items[k].count) source->items[k] = {}; moved = true; if (c.behaviors) c.behaviors->containerChanged(up); }
            }
        } else if (Cart* cart = c.carts ? c.carts->inventoryAt(up) : nullptr) {   // unload a storage cart above
            for (auto& slot : cart->items) {
                if (slot.empty()) continue;
                ItemStack one = slot; one.count = 1;
                if (insertOne(c, *be, hs, one, 0)) { slot.count--; if (!slot.count) slot = {}; moved = true; }
                break;
            }
        } else if (c.entities) {
            for (auto& e : c.entities->items) {
                if (e.removed || e.pos.x < h.p.x || e.pos.x >= h.p.x + 1 || e.pos.z < h.p.z || e.pos.z >= h.p.z + 1 || e.pos.y < h.p.y + 0.9f || e.pos.y > h.p.y + 1.6f) continue;
                while (!e.stack.empty()) {
                    ItemStack one = e.stack; one.count = 1;
                    if (!insertOne(c, *be, hs, one, 0)) break;
                    e.stack.count--; moved = true;
                }
                if (e.stack.empty()) e.removed = true;
            }
        }
        if (moved) {
            be->data[0] = HOPPER_COOLDOWN;
            if (ChunkPtr ch = c.world.chunk(h.p.chunk())) ch->unsaved = true;
            if (ChunkPtr ch = c.world.chunk(tp.chunk())) ch->unsaved = true;
            if (c.behaviors) c.behaviors->containerChanged(h.p);
        }
    }
    // hopper carts: pull from the container above the cart, or pick up items in the cart's cell
    if (c.carts) for (auto& cart : c.carts->carts) {
        if (cart.removed || cart.kind != CartKind::Hopper) continue;
        BlockPos cell{(int)std::floor(cart.pos.x), (int)std::floor(cart.pos.y + 0.01f), (int)std::floor(cart.pos.z)};
        if (!c.world.isReady(cell.chunk())) continue;
        if (cart.cooldown > 0) { cart.cooldown--; continue; }
        bool moved = false;
        BlockPos up{cell.x, cell.y + 1, cell.z};
        BlockState us; BlockEntity* source = containerAt(c, up, &us);
        if (source) {
            int k = pullSlot(c, *source, us);
            if (k >= 0 && insertInto(c, cart.items, source->items[k])) {
                if (!--source->items[k].count) source->items[k] = {};
                moved = true;
                if (ChunkPtr ch = c.world.chunk(up.chunk())) ch->unsaved = true;
                if (c.behaviors) c.behaviors->containerChanged(up);
            }
        } else if (c.entities) {
            for (auto& e : c.entities->items) {
                if (e.removed || std::fabs(e.pos.x - cart.pos.x) > 0.6f || std::fabs(e.pos.z - cart.pos.z) > 0.6f || e.pos.y < cart.pos.y - 0.1f || e.pos.y > cart.pos.y + 1.2f) continue;
                while (!e.stack.empty() && insertInto(c, cart.items, e.stack)) { e.stack.count--; moved = true; }
                if (e.stack.empty()) e.removed = true;
            }
        }
        if (moved) cart.cooldown = CartSystem::HOPPER_COOLDOWN;
    }
    if (c.entities) c.entities->items.erase(std::remove_if(c.entities->items.begin(), c.entities->items.end(), [](const ItemEntity& e) { return e.removed; }), c.entities->items.end());
}

void dispense(ContainerContext& c, const BlockPos& p, bool dispenser, const BlockPos& dir) {
    BlockEntity* be = containerAt(c, p);
    if (!be) return;
    std::vector<int> full;
    for (size_t i = 0; i < be->items.size(); i++) if (!be->items[i].empty()) full.push_back((int)i);
    if (full.empty()) return;
    int k = full[c.rng ? (size_t)c.rng->range((int)full.size()) : 0];
    ItemStack& slot = be->items[k];
    BlockPos front = p + dir;
    Vec3 out{front.x + 0.5f - dir.x * 0.3f, front.y + 0.4f - dir.y * 0.3f, front.z + 0.5f - dir.z * 0.3f};
    Vec3 vel{dir.x * 0.25f, dir.y * 0.25f + 0.05f, dir.z * 0.25f};
    const std::string& name = c.items.def(slot.item).name;
    if (ChunkPtr ch = c.world.chunk(p.chunk())) ch->unsaved = true;
    if (dispenser && (name == "water_bucket" || name == "lava_bucket") && c.behaviors) {
        if (c.world.reg.def(c.world.getBlock(front)).has(BF_REPLACEABLE)) { c.behaviors->placeFluidSource(front, name == "lava_bucket"); slot = {c.items.id("bucket"), 1, 0}; }
        return;
    }
    if (dispenser && name == "bucket") {
        BlockState f = c.world.getBlock(front);
        if (c.world.reg.fluid(f) && c.world.reg.getProp(f, "level") == 0) {
            bool lava = c.world.reg.def(f).name == "lava";
            c.world.setBlock(front, AIR);
            slot.count--; ItemStack full{c.items.id(lava ? "lava_bucket" : "water_bucket"), 1, 0};
            if (!slot.count) slot = full;
            else { Inventory tmp; tmp.slots = be->items; if (tmp.add(full, c.items) == 0) be->items = tmp.slots; else if (c.entities && c.rng) c.entities->spawnItem(out, full, *c.rng, vel); }
        }
        return;
    }
    if (dispenser && name == "arrow" && c.shootArrow) {
        c.shootArrow(out, Vec3(dir.x * 1.6f, dir.y * 1.6f + 0.1f, dir.z * 1.6f));
        slot.count--; if (!slot.count) slot = {};
        return;
    }
    // drop one item (droppers into a container in front insert instead)
    BlockState ts; BlockEntity* target = dispenser ? nullptr : containerAt(c, front, &ts);
    ItemStack one = slot; one.count = 1;
    if (target) { be = c.world.blockEntity(p); if (insertOne(c, *target, ts, one, 1)) { be->items[k].count--; if (!be->items[k].count) be->items[k] = {}; } return; }
    if (c.entities && c.rng) { c.entities->spawnItem(out, one, *c.rng, vel); c.entities->items.back().pickupDelay = 10; }
    slot.count--; if (!slot.count) slot = {};
}

} // namespace vx
