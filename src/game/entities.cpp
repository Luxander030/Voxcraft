#include "game/entities.h"

namespace vx {

void EntityManager::spawnItem(Vec3 pos, ItemStack s, Rng& rng, Vec3 v) {
    if (s.empty()) return;
    ItemEntity e; e.pos = pos; e.stack = s;
    e.vel = v.x == 0 && v.y == 0 && v.z == 0 ? Vec3{(rng.unit() - 0.5f) * 0.1f, 0.2f, (rng.unit() - 0.5f) * 0.1f} : v;
    items.push_back(e);
}

void EntityManager::tick(const World& w, const Player& player, Inventory* inv) {
    const float hw = 0.125f;
    for (auto& e : items) {
        if (e.removed) continue;
        if (!w.isReady({(int)std::floor(e.pos.x) >> 4, (int)std::floor(e.pos.z) >> 4})) continue;   // frozen at the edge of the world
        e.age++; if (e.pickupDelay > 0) e.pickupDelay--;
        if (e.age > DESPAWN_TICKS || e.pos.y < -64) { e.removed = true; continue; }
        bool inFluid = w.reg.fluid(w.getBlock((int)std::floor(e.pos.x), (int)std::floor(e.pos.y + 0.1f), (int)std::floor(e.pos.z)));
        e.vel.y = inFluid ? e.vel.y * 0.8f + 0.01f : (e.vel.y - 0.04f) * 0.98f;
        bool hit;
        for (int axis : {1, 0, 2}) {
            AABB b{{e.pos.x - hw, e.pos.y, e.pos.z - hw}, {e.pos.x + hw, e.pos.y + 0.25f, e.pos.z + hw}};
            float d = sweepAxis(w, b, axis, e.vel[axis], hit);
            e.pos[axis] += d;
            if (hit) e.vel[axis] = axis == 1 ? 0 : -e.vel[axis] * 0.2f;
        }
        bool ground = e.vel.y == 0;
        float fr = ground ? 0.6f : 0.98f;
        e.vel.x *= fr; e.vel.z *= fr;
        // pick up
        if (inv && e.pickupDelay == 0 && !player.flying) {
            Vec3 d = e.pos - (player.pos + Vec3(0, 0.9f, 0));
            if (dot(d, d) < 1.5f * 1.5f) {
                int before = e.stack.count;
                int left = inv->add(e.stack, items_);
                if (left < before && onPickup) { ItemStack got = e.stack; got.count = (u8)(before - left); onPickup(got); }
                e.stack.count = (u8)left;
                if (!left) e.removed = true;
            }
        }
    }
    // merge nearby equal stacks (cheap: only every 10 ticks, O(n^2) on a small n)
    static int mergeTick = 0;
    if (++mergeTick % 10 == 0) {
        for (size_t i = 0; i < items.size(); i++) for (size_t j = i + 1; j < items.size(); j++) {
            ItemEntity &a = items[i], &b = items[j];
            if (a.removed || b.removed || !canMerge(a.stack, b.stack, items_)) continue;
            Vec3 d = a.pos - b.pos;
            if (dot(d, d) > 1.5f * 1.5f) continue;
            int room = items_.maxStack(a.stack.item) - a.stack.count, n = std::min<int>(room, b.stack.count);
            if (n <= 0) continue;
            a.stack.count = (u8)(a.stack.count + n); b.stack.count = (u8)(b.stack.count - n);
            a.age = std::min(a.age, b.age);
            if (!b.stack.count) b.removed = true;
        }
    }
    items.erase(std::remove_if(items.begin(), items.end(), [](const ItemEntity& e) { return e.removed; }), items.end());
}

std::vector<EntityRecord> EntityManager::collectEntities(ChunkPos p, bool remove) {
    std::vector<EntityRecord> out;
    for (auto& e : items) {
        if (e.removed || ChunkPos{(int)std::floor(e.pos.x) >> 4, (int)std::floor(e.pos.z) >> 4} != p) continue;
        ByteWriter w; w.u8_(1);
        w.f64_(e.pos.x); w.f64_(e.pos.y); w.f64_(e.pos.z); w.f32_(e.vel.x); w.f32_(e.vel.y); w.f32_(e.vel.z);
        w.str(items_.def(e.stack.item).name); w.u8_(e.stack.count); w.var(e.stack.damage); w.var((u64)e.age);
        out.push_back({"item", std::move(w.buf)});
        if (remove) e.removed = true;
    }
    if (remove) items.erase(std::remove_if(items.begin(), items.end(), [](const ItemEntity& e) { return e.removed; }), items.end());
    if (mobs) {
        for (auto& m : mobs->mobs) {
            if (m.removed || !m.alive() || ChunkPos{(int)std::floor(m.pos.x) >> 4, (int)std::floor(m.pos.z) >> 4} != p) continue;
            ByteWriter w; w.u8_(3);
            w.str(mobs->registry().def(m.type).name);
            w.f64_(m.pos.x); w.f64_(m.pos.y); w.f64_(m.pos.z); w.f32_(m.yaw); w.f32_(m.health); w.var((u64)m.age);
            w.u8_(m.hasHome ? 1 : 0); w.f32_(m.home.x); w.f32_(m.home.y); w.f32_(m.home.z); w.u32_(m.seed);
            w.svar(m.growTicks); w.var((u64)m.loveTicks); w.var((u64)m.breedCooldown);   // v3
            out.push_back({"mob", std::move(w.buf)});
            if (remove) m.removed = true;
        }
        if (remove) mobs->mobs.erase(std::remove_if(mobs->mobs.begin(), mobs->mobs.end(), [](const Mob& m) { return m.removed; }), mobs->mobs.end());
    }
    if (carts) {
        for (auto& c : carts->carts) {
            if (c.removed || ChunkPos{(int)std::floor(c.pos.x) >> 4, (int)std::floor(c.pos.z) >> 4} != p) continue;
            ByteWriter w; w.u8_(2);
            w.f64_(c.pos.x); w.f64_(c.pos.y); w.f64_(c.pos.z); w.f32_(c.vel.x); w.f32_(c.vel.y); w.f32_(c.vel.z); w.f32_(c.yaw);
            w.u8_((u8)c.kind);   // v2: kind, then the non-empty storage slots
            u64 n = 0; for (auto& s : c.items) if (!s.empty()) n++;
            w.var(n);
            for (size_t i = 0; i < c.items.size(); i++) {
                const ItemStack& s = c.items[i];
                if (s.empty()) continue;
                w.u8_((u8)i); w.str(items_.def(s.item).name); w.u8_(s.count); w.var(s.damage);
            }
            out.push_back({"cart", std::move(w.buf)});
            if (remove) c.removed = true;
        }
        if (remove) carts->carts.erase(std::remove_if(carts->carts.begin(), carts->carts.end(), [](const Cart& c) { return c.removed; }), carts->carts.end());
    }
    return out;
}

void EntityManager::spawnEntities(ChunkPos, std::vector<EntityRecord>&& recs) {
    for (auto& r : recs) {
        if (r.type == "mob" && mobs) {
            ByteReader rd(r.payload);
            u8 ver = rd.u8_();
            if (ver < 1 || ver > 3) continue;
            u16 t = mobs->registry().id(rd.str());
            Vec3 pos; pos.x = (float)rd.f64_(); pos.y = (float)rd.f64_(); pos.z = (float)rd.f64_();
            float yaw = rd.f32_(), hp = rd.f32_(); int age = (int)rd.var();
            bool hasHome = false; Vec3 home; u32 seed = 0;
            if (ver >= 2) { hasHome = rd.u8_() != 0; home.x = rd.f32_(); home.y = rd.f32_(); home.z = rd.f32_(); seed = rd.u32_(); }
            int grow = 0, love = 0, cool = 0;
            if (ver >= 3) { grow = (int)rd.svar(); love = (int)rd.var(); cool = (int)rd.var(); }
            if (!rd.ok() || t == 0xFFFF) { VX_WARN("dropping unreadable mob record"); continue; }
            Rng rng((u64)age * 31 + 7);
            Mob& m = mobs->spawn(t, pos, rng); m.yaw = m.prevYaw = yaw; m.health = hp; m.age = age;
            m.hasHome = hasHome; m.home = home; m.seed = seed;
            m.growTicks = std::min(0, grow); m.loveTicks = love; m.breedCooldown = cool;
            continue;
        }
        if (r.type == "cart" && carts) {
            ByteReader rd(r.payload);
            u8 ver = rd.u8_();
            if (ver < 1 || ver > 2) continue;
            Vec3 pos, vel; pos.x = (float)rd.f64_(); pos.y = (float)rd.f64_(); pos.z = (float)rd.f64_();
            vel.x = rd.f32_(); vel.y = rd.f32_(); vel.z = rd.f32_(); float yaw = rd.f32_();
            CartKind kind = CartKind::Plain;
            std::vector<std::pair<int, ItemStack>> slots;
            if (ver >= 2) {
                u8 k = rd.u8_();
                kind = k == 1 ? CartKind::Storage : CartKind::Plain;
                u64 n = rd.var();
                for (u64 i = 0; i < n && rd.ok() && i < 256; i++) {
                    int slot = rd.u8_(); ItemStack s; s.item = items_.id(rd.str()); s.count = rd.u8_(); s.damage = (u16)rd.var();
                    if (s.item == 0) { VX_WARN("storage cart: unknown item dropped"); continue; }
                    slots.push_back({slot, s});
                }
            }
            if (!rd.ok()) { VX_WARN("dropping unreadable cart record"); continue; }
            Cart& c = carts->spawn(pos, kind); c.vel = vel; c.yaw = c.prevYaw = yaw;
            for (auto& [slot, s] : slots) if (slot < (int)c.items.size()) c.items[slot] = s;
            continue;
        }
        if (r.type != "item") { VX_WARN("unknown entity type '%s' dropped", r.type.c_str()); continue; }
        ByteReader rd(r.payload);
        if (rd.u8_() != 1) continue;
        ItemEntity e;
        e.pos.x = (float)rd.f64_(); e.pos.y = (float)rd.f64_(); e.pos.z = (float)rd.f64_();
        e.vel.x = rd.f32_(); e.vel.y = rd.f32_(); e.vel.z = rd.f32_();
        e.stack.item = items_.id(rd.str()); e.stack.count = rd.u8_(); e.stack.damage = (u16)rd.var(); e.age = (int)rd.var();
        e.pickupDelay = 0;
        if (rd.ok() && !e.stack.empty()) items.push_back(e);
    }
}

} // namespace vx
