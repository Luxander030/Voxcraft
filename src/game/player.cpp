#include "game/player.h"

namespace vx {

AABB Player::boxAt(const Vec3& p) const {
    float hw = tune.width * 0.5f, h = (sneaking && !flying) ? tune.sneakHeight : tune.height;
    return {{p.x - hw, p.y, p.z - hw}, {p.x + hw, p.y + h, p.z + hw}};
}
AABB Player::box() const { return boxAt(pos); }

Vec3 Player::look() const {
    float cp = std::cos(pitch);
    return {std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp};
}

// Visits every collision box (world space) of the blocks overlapping `r`. Full solid blocks are
// unit cubes; building shapes contribute their per-state boxes. One extra layer below is scanned
// because fences reach 1.5 blocks up. Cells below the world floor count as solid.
template <class F> static bool forEachCollider(const World& w, const AABB& r, F&& f) {
    const float eps = 1e-5f;
    int x0 = (int)std::floor(r.min.x), x1 = (int)std::floor(r.max.x - eps);
    int y0 = (int)std::floor(r.min.y) - 1, y1 = (int)std::floor(r.max.y - eps);
    int z0 = (int)std::floor(r.min.z), z1 = (int)std::floor(r.max.z - eps);
    for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        if (y < 0) { if (f(AABB{{(float)x, (float)y, (float)z}, {(float)x + 1, (float)y + 1, (float)z + 1}})) return true; continue; }
        BlockState s = w.getBlock(x, y, z);
        if (!w.reg.solid(s)) continue;
        if (!w.reg.boxed(s)) {
            if (y == y0 && y0 < (int)std::floor(r.min.y)) continue;   // the extra layer only matters for tall shapes
            if (f(AABB{{(float)x, (float)y, (float)z}, {(float)x + 1, (float)y + 1, (float)z + 1}})) return true;
            continue;
        }
        for (const ShapeBox& sb : w.reg.collisionBoxes(s)) {
            AABB a{{x + sb.mn[0] / 16.0f, y + sb.mn[1] / 16.0f, z + sb.mn[2] / 16.0f}, {x + sb.mx[0] / 16.0f, y + sb.mx[1] / 16.0f, z + sb.mx[2] / 16.0f}};
            if (f(a)) return true;
        }
    }
    return false;
}

bool Player::collides(const World& w, const AABB& b) const { return collidesAny(w, b); }

bool collidesAny(const World& w, const AABB& b) {
    return forEachCollider(w, b, [&](const AABB& a) { return a.intersects(b); });
}

float sweepAxis(const World& w, const AABB& b, int axis, float d, bool& hit) {
    // Exact sweep: clip the movement against every collision box that overlaps the moving box on
    // the other two axes and lies ahead of it along `axis`.
    hit = false;
    if (d == 0) return 0;
    const float eps = 1e-4f;
    int o1 = (axis + 1) % 3, o2 = (axis + 2) % 3;
    AABB region = b;
    if (d > 0) region.max[axis] += d; else region.min[axis] += d;
    float allowed = d;
    forEachCollider(w, region, [&](const AABB& c) {
        if (c.max[o1] <= b.min[o1] + eps || c.min[o1] >= b.max[o1] - eps) return false;
        if (c.max[o2] <= b.min[o2] + eps || c.min[o2] >= b.max[o2] - eps) return false;
        if (d > 0 && c.min[axis] >= b.max[axis] - eps) { float a = c.min[axis] - b.max[axis]; if (a < allowed) { allowed = a; hit = true; } }
        else if (d < 0 && c.max[axis] <= b.min[axis] + eps) { float a = c.max[axis] - b.min[axis]; if (a > allowed) { allowed = a; hit = true; } }
        return false;
    });
    return d > 0 ? std::max(0.0f, std::min(allowed, d)) : std::min(0.0f, std::max(allowed, d));
}

void Player::moveAxis(const World& w, int axis, float d, bool& hit) { pos[axis] += sweepAxis(w, box(), axis, d, hit); }

void Player::tick(const World& w, const PlayerInput& in) {
    prevPos = pos;
    if (in.toggleFly && canFly) { flying = !flying; vel = {}; }
    sneaking = in.sneak && !flying;
    sprinting = in.sprint && in.forward > 0 && !sneaking;

    // environment
    AABB b = box();
    inFluid = headInFluid = onClimbable = false;
    for (int y = (int)std::floor(b.min.y); y <= (int)std::floor(b.max.y); y++)
        for (int z = (int)std::floor(b.min.z); z <= (int)std::floor(b.max.z - 1e-5f); z++)
            for (int x = (int)std::floor(b.min.x); x <= (int)std::floor(b.max.x - 1e-5f); x++) {
                BlockState s = w.getBlock(x, y, z);
                if (w.reg.fluid(s)) { inFluid = true; if (y == (int)std::floor(eye().y)) headInFluid = true; }
                if (w.reg.def(s).has(BF_CLIMBABLE)) onClimbable = true;
            }

    // desired horizontal velocity in world space
    float f = clamp(in.forward, -1.0f, 1.0f), s = clamp(in.strafe, -1.0f, 1.0f);
    float len = std::sqrt(f * f + s * s); if (len > 1) { f /= len; s /= len; }
    float sy = std::sin(yaw), cy = std::cos(yaw);
    Vec3 wish{sy * f + cy * s, 0, -cy * f + sy * s};

    if (flying) {
        float sp = tune.flySpeed * (in.sprint ? 1.6f : 1.0f);
        vel.x += (wish.x * sp - vel.x) * 0.25f; vel.z += (wish.z * sp - vel.z) * 0.25f;
        float vy = (in.jump ? 1.f : 0.f) - (in.sneak ? 1.f : 0.f);
        vel.y += (vy * tune.flyVertical - vel.y) * 0.4f;
    } else {
        float speed = tune.walk * (sprinting ? tune.sprintMul : 1.0f) * (sneaking ? tune.sneakMul : 1.0f);
        if (inFluid) speed *= 0.5f;
        float grip = onGround ? tune.groundGrip : (inFluid ? 0.2f : tune.airGrip);
        vel.x += (wish.x * speed - vel.x) * grip; vel.z += (wish.z * speed - vel.z) * grip;
        if (inFluid) {
            vel.y = vel.y * tune.swimDrag - tune.swimGravity;
            if (in.jump) vel.y += tune.swimUp + tune.swimGravity;
        } else if (onClimbable) {
            vel.y = std::max(vel.y - tune.gravity, -tune.climbMaxFall);
            if (in.jump || (std::fabs(f) > 0.1f && !onGround)) vel.y = tune.climb;
            if (sneaking && vel.y < 0) vel.y = 0;   // hold position on ladders
        } else {
            if (in.jump && onGround) vel.y = tune.jump;   // jump tick moves the full impulse
            else vel.y = std::max((vel.y - tune.gravity) * tune.verticalDrag, -tune.maxFall);
        }
    }

    // move: Y first (lands before sliding), then X and Z
    bool hitY, hitX, hitZ;
    float oldY = pos.y;
    moveAxis(w, 1, vel.y, hitY);
    bool wasGround = onGround;
    onGround = hitY && vel.y < 0;
    if (hitY) vel.y = 0;
    const Vec3 beforeH = pos; const float wantX = vel.x, wantZ = vel.z;
    for (int axis : {0, 2}) {
        float d = vel[axis];
        if (sneaking && onGround && d != 0) {
            // edge guard: never step off a ledge while sneaking
            Vec3 t = pos; t[axis] += d;
            AABB below = boxAt(t); below.min.y -= 0.6f; below.max.y = pos.y;
            if (!collides(w, below)) { vel[axis] = 0; continue; }
        }
        moveAxis(w, axis, d, axis == 0 ? hitX : hitZ);
        if ((axis == 0 ? hitX : hitZ)) vel[axis] = 0;
    }
    // Step up onto slabs, stairs and other low ledges (at most tune.stepHeight) without jumping:
    // retry the horizontal move from a raised position, settle back down, and keep it if it got
    // further than the plain move.
    if (onGround && (hitX || hitZ) && tune.stepHeight > 0) {
        Vec3 t = beforeH; bool h;
        float up = sweepAxis(w, boxAt(t), 1, tune.stepHeight, h); t.y += up;
        bool sx, sz;
        t.x += sweepAxis(w, boxAt(t), 0, wantX, sx);
        t.z += sweepAxis(w, boxAt(t), 2, wantZ, sz);
        t.y += sweepAxis(w, boxAt(t), 1, -up, h);
        float gotStep = (t.x - beforeH.x) * (t.x - beforeH.x) + (t.z - beforeH.z) * (t.z - beforeH.z);
        float gotFlat = (pos.x - beforeH.x) * (pos.x - beforeH.x) + (pos.z - beforeH.z) * (pos.z - beforeH.z);
        if (gotStep > gotFlat + 1e-6f && t.y > beforeH.y - 1e-4f) {
            pos = t; vel.x = sx ? 0 : wantX; vel.z = sz ? 0 : wantZ;
        }
    }
    if (flying && onGround) flying = false;   // landing ends flight

    // fall distance: accumulated while descending freely, reported once on landing
    landedFall = -1;
    if (inFluid || onClimbable || flying) fallDistance = 0;
    else if (pos.y < oldY) fallDistance += oldY - pos.y;
    if (onGround && !wasGround) { landedFall = fallDistance; fallDistance = 0; }
}

RayHit raycastBlocks(const World& w, Vec3 o, Vec3 d, float reach, bool hitFluidSources) {
    RayHit r;
    d = normalize(d);
    int x = (int)std::floor(o.x), y = (int)std::floor(o.y), z = (int)std::floor(o.z);
    int stepX = d.x > 0 ? 1 : -1, stepY = d.y > 0 ? 1 : -1, stepZ = d.z > 0 ? 1 : -1;
    auto inv = [](float v) { return std::fabs(v) < 1e-9f ? 1e30f : std::fabs(1.0f / v); };
    float tdx = inv(d.x), tdy = inv(d.y), tdz = inv(d.z);
    auto first = [](float p, float dd, int c) { float b = dd > 0 ? (float)(c + 1) - p : p - (float)c; return b; };
    float tmx = first(o.x, d.x, x) * tdx, tmy = first(o.y, d.y, y) * tdy, tmz = first(o.z, d.z, z) * tdz;
    BlockPos n{0, 0, 0};
    float t = 0;
    while (t <= reach) {
        BlockState s = w.getBlock(x, y, z);
        bool fluidHit = hitFluidSources && w.reg.fluid(s) && w.reg.getProp(s, "level") == 0;
        if ((s != AIR && !w.reg.fluid(s)) || fluidHit) {
            if (w.reg.boxed(s)) {   // building shapes: hit only their actual boxes (aim through an open door)
                float best = 1e30f; BlockPos bn = n;
                for (const ShapeBox& b : w.reg.renderBoxes(s)) {
                    float tmin = 0, tmax = 1e30f; int axisHit = -1; bool ok = true;
                    for (int a = 0; a < 3 && ok; a++) {
                        float lo = (a == 0 ? x : a == 1 ? y : z) + b.mn[a] / 16.0f, hi = (a == 0 ? x : a == 1 ? y : z) + b.mx[a] / 16.0f;
                        if (std::fabs(d[a]) < 1e-9f) { if (o[a] < lo || o[a] > hi) ok = false; continue; }
                        float t0 = (lo - o[a]) / d[a], t1 = (hi - o[a]) / d[a];
                        if (t0 > t1) std::swap(t0, t1);
                        if (t0 > tmin) { tmin = t0; axisHit = a; }
                        tmax = std::min(tmax, t1);
                        if (tmin > tmax) ok = false;
                    }
                    if (ok && tmin < best && tmin <= reach) {
                        best = tmin;
                        bn = {0, 0, 0};
                        if (axisHit >= 0) { int sgn = d[axisHit] > 0 ? -1 : 1; if (axisHit == 0) bn.x = sgn; else if (axisHit == 1) bn.y = sgn; else bn.z = sgn; }
                        else bn = n;
                    }
                }
                if (best < 1e29f) { r.block = {x, y, z}; r.normal = bn; r.distance = best; r.hit = true; r.point = o + d * best; return r; }
            } else { r.block = {x, y, z}; r.normal = n; r.distance = t; r.hit = true; r.point = o + d * t; return r; }
        }
        if (tmx < tmy && tmx < tmz) { x += stepX; t = tmx; tmx += tdx; n = {-stepX, 0, 0}; }
        else if (tmy < tmz) { y += stepY; t = tmy; tmy += tdy; n = {0, -stepY, 0}; }
        else { z += stepZ; t = tmz; tmz += tdz; n = {0, 0, -stepZ}; }
    }
    return r;
}

} // namespace vx
