#include "game/carts.h"
#include "game/mobs.h"

namespace vx {

int cartSlots(CartKind k) { return k == CartKind::Storage ? CartSystem::STORAGE_SLOTS : k == CartKind::Hopper ? CartSystem::HOPPER_SLOTS : 0; }

Cart& CartSystem::spawn(Vec3 pos, CartKind kind) {
    Cart c; c.id = nextId++; c.pos = c.prevPos = pos; c.kind = kind;
    c.items.resize(cartSlots(kind));
    carts.push_back(c);
    return carts.back();
}

Cart* CartSystem::inventoryAt(const BlockPos& p) {
    AABB cell{{(float)p.x, (float)p.y, (float)p.z}, {p.x + 1.0f, p.y + 1.0f, p.z + 1.0f}};
    for (auto& c : carts) {
        if (c.removed || c.items.empty()) continue;
        AABB b = c.box();
        // the cart's centre column must be in the cell (so one cart is never served by two hoppers side by side)
        if (c.pos.x < cell.min.x || c.pos.x >= cell.max.x || c.pos.z < cell.min.z || c.pos.z >= cell.max.z) continue;
        if (b.min.y < cell.max.y && b.max.y > cell.min.y) return &c;
    }
    return nullptr;
}

Cart* CartSystem::byId(u32 id) { for (auto& c : carts) if (c.id == id && !c.removed) return &c; return nullptr; }

bool CartSystem::railCell(const World& w, Vec3 p, BlockPos& cell, BlockState& s) const {
    cell = {(int)std::floor(p.x), (int)std::floor(p.y + 0.01f), (int)std::floor(p.z)};
    s = w.getBlock(cell);
    if (isRail(w.reg, s)) return true;
    cell.y -= 1; s = w.getBlock(cell);   // the top end of a slope belongs to the cell below
    return isRail(w.reg, s);
}

void CartSystem::step(const World& w, Cart& c) {
    c.prevPos = c.pos; c.prevYaw = c.yaw; c.prevPitch = c.pitch;
    if (c.hurtTicks > 0) c.hurtTicks--;
    if (c.pickupDelay > 0) c.pickupDelay--;
    BlockPos cell; BlockState s;
    c.onRail = railCell(w, c.pos, cell, s);
    c.onTrip = c.onRail && isTripRail(w.reg, s) && w.reg.getProp(s, "active");
    if (c.onRail) {
        int shape = w.reg.getProp(s, "shape");
        RailGeom g = railGeometry(shape);
        Vec3 base{(float)cell.x, (float)cell.y, (float)cell.z};
        Vec3 A = base + g.a, B = base + g.b, u = B - A;
        float len = length(u); Vec3 uh = u * (1.0f / len);
        float t = clamp(dot(c.pos - A, u) / (len * len), 0.0f, 1.0f);
        // signed speed along A->B; the magnitude is kept even if the velocity points elsewhere
        // (it came out of a curve or off another rail), so curves never bleed speed
        float sp = length(c.vel), dir = dot(c.vel, uh);
        float s1 = dir >= 0 ? sp : -sp;
        if (sp < 1e-6f) s1 = 0;
        s1 += dot(c.push, uh);   // collision impulses act only along the track
        c.push = {};
        if (u.y != 0) s1 -= SLOPE_G * (u.y / len);   // downhill accelerates, uphill slows
        if (isPoweredRail(w.reg, s)) {
            if (w.reg.getProp(s, "powered")) { if (std::fabs(s1) > 1e-3f) s1 += s1 > 0 ? BOOST : -BOOST; }
            else s1 *= 0.5f;   // unpowered: brake
        }
        s1 *= FRICTION;
        s1 = clamp(s1, -MAX_SPEED, MAX_SPEED);
        if (std::fabs(s1) < 1e-4f) s1 = 0;
        float nt = t + s1 / len;
        if (nt >= 0 && nt <= 1) {
            c.pos = A + u * nt;
            c.vel = uh * s1;
        } else {
            // run past an end: continue straight out of that end into the next cell
            bool atB = nt > 1;
            float over = (atB ? nt - 1 : -nt) * len;
            int outDir = atB ? g.dirB : g.dirA;
            int dx, dz; dirOffset(outDir, dx, dz);
            Vec3 out{(float)dx, 0, (float)dz};
            c.pos = (atB ? B : A) + out * std::min(over, 0.45f);
            c.vel = out * std::fabs(s1);
            BlockPos nc; BlockState ns;
            if (!railCell(w, c.pos + Vec3(0, 0.01f, 0), nc, ns)) { c.onRail = false; }
        }
        if (std::fabs(c.vel.x) + std::fabs(c.vel.z) > 1e-4f) c.yaw = std::atan2(c.vel.x, -c.vel.z);
        else if (u.x != 0 || u.z != 0) { float ry = std::atan2(uh.x, -uh.z); if (std::fabs(std::remainder(ry - c.yaw, PI)) > 0.1f) c.yaw = ry; }
        c.pitch = std::atan2(u.y, std::sqrt(u.x * u.x + u.z * u.z)) * (dot(Vec3(std::sin(c.yaw), 0, -std::cos(c.yaw)), uh) >= 0 ? 1.0f : -1.0f);
        return;
    }
    // off the rails: fall and slide
    c.pitch = 0;
    c.vel += Vec3(c.push.x, 0, c.push.z); c.push = {};
    c.vel.y = std::max(c.vel.y - 0.04f, -2.0f);
    bool h;
    AABB b = c.box();
    c.pos.y += sweepAxis(w, b, 1, c.vel.y, h);
    bool ground = h && c.vel.y < 0;
    if (h) c.vel.y = 0;
    bool hx, hz;
    c.pos.x += sweepAxis(w, c.box(), 0, c.vel.x, hx); if (hx) c.vel.x = 0;
    c.pos.z += sweepAxis(w, c.box(), 2, c.vel.z, hz); if (hz) c.vel.z = 0;
    float fr = ground ? 0.5f : 0.95f;
    c.vel.x *= fr; c.vel.z *= fr;
}

void CartSystem::tick(const World& w) {
    for (auto& c : carts) {
        if (c.removed) continue;
        if (!w.isReady({(int)std::floor(c.pos.x) >> 4, (int)std::floor(c.pos.z) >> 4})) continue;   // frozen at the world edge
        step(w, c);
        if (c.pos.y < -32) c.removed = true;
        if (onRail && c.onRail) { BlockPos cell; BlockState s; if (railCell(w, c.pos, cell, s)) onRail(cell, s); }
    }
    collideCarts();
    carts.erase(std::remove_if(carts.begin(), carts.end(), [](const Cart& c) { return c.removed; }), carts.end());
}

namespace {
bool overlaps(const AABB& a, const AABB& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y && a.min.z < b.max.z && a.max.z > b.min.z;
}
// Horizontal unit direction between two centres (pushes never lift or sink anything).
Vec3 flatNormal(Vec3 from, Vec3 to, float tieBreak) {
    Vec3 d{to.x - from.x, 0, to.z - from.z};
    float l = length(d);
    if (l < 1e-4f) return {std::cos(tieBreak), 0, std::sin(tieBreak)};   // exactly stacked: any fixed direction
    return d * (1.0f / l);
}
}

void CartSystem::collideCarts() {
    for (size_t i = 0; i < carts.size(); i++) for (size_t j = i + 1; j < carts.size(); j++) {
        Cart& a = carts[i]; Cart& b = carts[j];
        if (a.removed || b.removed) continue;
        if (std::fabs(a.pos.x - b.pos.x) >= 0.98f || std::fabs(a.pos.z - b.pos.z) >= 0.98f) continue;   // cheap reject
        if (!overlaps(a.box(), b.box())) continue;
        Vec3 n = flatNormal(a.pos, b.pos, (float)(a.id * 2654435761u % 628) * 0.01f);
        float closing = dot(a.vel - b.vel, n);
        if (closing > 0) { a.push -= n * closing; b.push += n * closing; }   // elastic, equal masses
        // separation keeps resting carts from sitting inside each other
        float gap = 0.98f - std::max(std::fabs(a.pos.x - b.pos.x), std::fabs(a.pos.z - b.pos.z));
        Vec3 sep = n * std::min(gap * 0.1f, 0.02f);
        a.push -= sep; b.push += sep;
    }
}

bool CartSystem::contact(const Cart& c, const AABB& body, Vec3 bodyVel, Vec3& normal, float& approach) {
    if (c.removed || !overlaps(c.box(), body)) return false;
    Vec3 centre = (body.min + body.max) * 0.5f;
    normal = flatNormal(c.pos, centre, (float)(c.id % 628) * 0.01f);
    approach = dot(c.vel - Vec3(bodyVel.x, 0, bodyVel.z), normal);
    return true;
}

const char* cartItemName(CartKind k) { return k == CartKind::Storage ? "storage_cart" : k == CartKind::Hopper ? "hopper_cart" : "cart"; }
bool cartKindFromItem(const std::string& name, CartKind& k) {
    if (name == "cart") k = CartKind::Plain; else if (name == "storage_cart") k = CartKind::Storage; else if (name == "hopper_cart") k = CartKind::Hopper; else return false;
    return true;
}

std::vector<ItemStack> cartDrops(const Cart& c, const ItemRegistry& items, bool creative) {
    std::vector<ItemStack> out;
    if (!creative) out.push_back({items.id(cartItemName(c.kind)), 1, 0});
    for (const ItemStack& s : c.items) if (!s.empty()) out.push_back(s);
    return out;
}

void collideCartsWithMobs(CartSystem& cs, MobSystem& ms, u32 playerCart) {
    for (auto& c : cs.carts) c.carriesMob = false;
    // riders: follow their cart, or get off
    for (auto& m : ms.mobs) {
        if (m.boardOnLoad) {   // saved riding: board the cart it was saved in
            m.boardOnLoad = false;
            auto claimed = [&](u32 id) { for (auto& o : ms.mobs) if (o.cartId == id) return true; return false; };
            for (auto& c : cs.carts)
                if (!c.removed && c.kind == CartKind::Plain && !claimed(c.id) && std::fabs(c.pos.x - m.pos.x) < 0.6f && std::fabs(c.pos.z - m.pos.z) < 0.6f && std::fabs(c.pos.y - m.pos.y) < 1.0f) { m.cartId = c.id; break; }
        }
        if (!m.cartId) continue;
        Cart* c = cs.byId(m.cartId);
        if (m.removed || !m.alive() || !c || c->onTrip || c->carriesMob) {
            if (c && !m.removed) {   // step off to the side of the track
                c->pickupDelay = 20;
                Vec3 side{std::cos(c->yaw), 0, std::sin(c->yaw)};
                m.vel = side * 0.25f + Vec3(0, 0.2f, 0);
            }
            m.cartId = 0;
            continue;
        }
        c->carriesMob = true;
        Vec3 off{0, 0.2f, 0};
        m.prevPos = c->prevPos + off; m.pos = c->pos + off; m.vel = {}; m.onGround = true;
        m.prevYaw = c->prevYaw; m.yaw = c->yaw;
    }
    if (cs.carts.empty()) return;
    for (auto& m : ms.mobs) {
        if (m.removed || !m.alive() || m.cartId) continue;
        const MobDef& d = ms.registry().def(m.type);
        AABB mb = m.box(d);
        for (auto& c : cs.carts) {
            Vec3 n; float approach;
            if (!CartSystem::contact(c, mb, m.vel, n, approach)) continue;
            float speed = std::sqrt(c.vel.x * c.vel.x + c.vel.z * c.vel.z);
            if (c.kind == CartKind::Plain && !c.carriesMob && c.id != playerCart && c.pickupDelay == 0 && !c.onTrip &&
                speed > 0.05f && !d.flying && d.width * m.scale() <= 1.0f) {
                m.cartId = c.id; c.carriesMob = true; m.path.clear();
                m.prevPos = m.pos = c.pos + Vec3(0, 0.2f, 0); m.vel = {};
                break;
            }
            if (approach > 0) {
                m.vel += n * approach;
                if (m.onGround) m.vel.y = std::max(m.vel.y, 0.2f);   // bumped off its feet
                c.push -= n * (approach * CartSystem::CREATURE_MASS * 0.5f);
            }
            m.vel += n * 0.03f; c.push -= n * 0.005f;
        }
    }
}

Vec3 collideCartsWithPlayer(CartSystem& cs, const AABB& pb, Vec3 pv, u32 ridden) {
    Vec3 out{};
    for (auto& c : cs.carts) {
        if (c.id == ridden) continue;
        Vec3 n; float approach;
        if (!CartSystem::contact(c, pb, pv, n, approach)) continue;
        (void)approach;
        float cartToward = dot(c.vel, n);              // the cart's own motion, not the player's walking
        if (cartToward > 0.08f) out += n * cartToward;   // a fast cart shoves the player
        else c.push -= n * 0.015f;                      // walking into it nudges it
    }
    return out;
}

Cart* CartSystem::raycast(Vec3 o, Vec3 dir, float reach, float* outDist) {
    Cart* best = nullptr; float bestT = reach;
    for (auto& c : carts) {
        if (c.removed) continue;
        AABB b = c.box();
        float t0 = 0, t1 = bestT; bool hit = true;
        for (int a = 0; a < 3 && hit; a++) {
            float inv = std::fabs(dir[a]) < 1e-9f ? 1e30f : 1.0f / dir[a];
            float ta = (b.min[a] - o[a]) * inv, tb = (b.max[a] - o[a]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) hit = false;
        }
        if (hit && t0 < bestT) { bestT = t0; best = &c; }
    }
    if (outDist) *outDist = bestT;
    return best;
}

} // namespace vx
