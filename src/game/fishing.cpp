#include "game/fishing.h"

namespace vx {

namespace {
bool isWater(const World& w, BlockPos p) { BlockState s = w.getBlock(p); return s != AIR && w.reg.def(s).name == "water"; }
bool isLava(const World& w, BlockPos p) { BlockState s = w.getBlock(p); return s != AIR && w.reg.def(s).name == "lava"; }
BlockPos cellOf(Vec3 p) { return {(int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z)}; }
// The water surface height at the bobber's column (top of the highest connected water cell).
float surfaceAt(const World& w, Vec3 p) {
    BlockPos c = cellOf(p);
    while (isWater(w, {c.x, c.y + 1, c.z})) c.y++;
    return (float)c.y + 0.9f;
}
int newWait(const World& w, const Bobber& b, Rng& rng) {
    int t = Fishing::MIN_WAIT + rng.range(Fishing::MAX_WAIT - Fishing::MIN_WAIT + 1);
    BlockPos c = cellOf(b.pos);
    for (int y = c.y + 1; y < c.y + 24; y++) {   // covered water bites half as often
        BlockState s = w.getBlock({c.x, y, c.z});
        if (s != AIR && w.reg.opaque(s)) { t *= 2; break; }
    }
    return t;
}
} // namespace

const std::vector<CatchEntry>& Fishing::table() {
    static const std::vector<CatchEntry> t = {
        {"silverfin", 60, 1}, {"glowgill", 15, 1},                          // fish: 75%
        {"stick", 8, 1}, {"hide", 5, 1}, {"shadow_thread", 7, 2},          // junk: 20%
        {"azurite", 1, 1}, {"gold_ingot", 2, 1}, {"ember_shard", 2, 1},    // treasure: 5%
    };
    return t;
}

Bobber Fishing::cast(Vec3 eye, Vec3 look, Rng& rng) {
    Bobber b;
    b.pos = b.prevPos = eye + look * 0.4f;
    b.vel = look * 0.6f + Vec3((rng.unit() - 0.5f) * 0.02f, 0.1f, (rng.unit() - 0.5f) * 0.02f);
    return b;
}

bool Fishing::tick(const World& w, Bobber& b, Vec3 holder, Rng& rng) {
    if (b.removed) return false;
    b.prevPos = b.pos; b.age++;
    Vec3 d = b.pos - holder;
    if (b.age > MAX_AGE || dot(d, d) > MAX_LINE * MAX_LINE || b.pos.y < -64 || isLava(w, cellOf(b.pos))) { b.removed = true; return false; }
    bool water = isWater(w, cellOf(b.pos)) || isWater(w, cellOf(b.pos - Vec3(0, 0.15f, 0)));
    if (water) {
        if (!b.inWater) { b.inWater = true; b.wait = newWait(w, b, rng); b.vel.x *= 0.3f; b.vel.z *= 0.3f; }
        float target = surfaceAt(w, b.pos - Vec3(0, 0.15f, 0)) - 0.1f - (b.bite > 0 ? 0.25f : 0.0f);
        b.vel.y = (target - b.pos.y) * 0.25f;        // bob to the surface (dipped while a fish is on)
        b.vel.x *= 0.8f; b.vel.z *= 0.8f;
    } else {
        b.inWater = false; b.bite = 0;
        b.vel.y -= 0.03f;
        b.vel = b.vel * 0.92f;
    }
    AABB box{{b.pos.x - BOX, b.pos.y - BOX, b.pos.z - BOX}, {b.pos.x + BOX, b.pos.y + BOX, b.pos.z + BOX}};
    bool hy, hx, hz;
    float dy = sweepAxis(w, box, 1, b.vel.y, hy); b.pos.y += dy; box.min.y += dy; box.max.y += dy;
    if (hy) { b.vel.y = 0; b.vel.x *= 0.5f; b.vel.z *= 0.5f; }
    float dx = sweepAxis(w, box, 0, b.vel.x, hx); b.pos.x += dx; box.min.x += dx; box.max.x += dx;
    float dz = sweepAxis(w, box, 2, b.vel.z, hz); b.pos.z += dz;
    if (hx) b.vel.x = 0;
    if (hz) b.vel.z = 0;
    if (!b.inWater) return false;
    if (b.bite > 0) { if (--b.bite == 0) b.wait = newWait(w, b, rng); return false; }   // got away
    if (--b.wait <= 0) { b.bite = BITE_WINDOW; return true; }
    return false;
}

ItemStack Fishing::reel(Bobber& b, const ItemRegistry& items, Rng& rng) {
    bool hooked = b.inWater && b.bite > 0 && !b.removed;
    b.removed = true;
    if (!hooked) return {};
    int total = 0; for (auto& e : table()) total += e.weight;
    int r = rng.range(total);
    for (auto& e : table()) {
        if (r < e.weight) return {items.id(e.item), (u8)e.count, 0};
        r -= e.weight;
    }
    return {};
}

} // namespace vx
