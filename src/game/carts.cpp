#include "game/carts.h"

namespace vx {

Cart& CartSystem::spawn(Vec3 pos) {
    Cart c; c.id = nextId++; c.pos = c.prevPos = pos;
    carts.push_back(c);
    return carts.back();
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
    BlockPos cell; BlockState s;
    c.onRail = railCell(w, c.pos, cell, s);
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
    }
    carts.erase(std::remove_if(carts.begin(), carts.end(), [](const Cart& c) { return c.removed; }), carts.end());
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
