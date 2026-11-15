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

bool Player::collides(const World& w, const AABB& b) const {
    int x0 = (int)std::floor(b.min.x), x1 = (int)std::floor(b.max.x - 1e-5f);
    int y0 = (int)std::floor(b.min.y), y1 = (int)std::floor(b.max.y - 1e-5f);
    int z0 = (int)std::floor(b.min.z), z1 = (int)std::floor(b.max.z - 1e-5f);
    for (int y = y0; y <= y1; y++) for (int z = z0; z <= z1; z++) for (int x = x0; x <= x1; x++) {
        if (y < 0) return true;
        if (w.reg.solid(w.getBlock(x, y, z))) return true;
    }
    return false;
}

void Player::moveAxis(const World& w, int axis, float d, bool& hit) {
    // Exact sweep: find the nearest solid block face in the swept slab along `axis`.
    hit = false;
    if (d == 0) return;
    AABB b = box();
    const float eps = 1e-4f;
    int o1 = (axis + 1) % 3, o2 = (axis + 2) % 3;
    int a0 = (int)std::floor(b.min[o1] + eps), a1 = (int)std::floor(b.max[o1] - eps);
    int c0 = (int)std::floor(b.min[o2] + eps), c1 = (int)std::floor(b.max[o2] - eps);
    float allowed = d;
    int start, end, step;
    if (d > 0) { start = (int)std::floor(b.max[axis] - eps) + 1; end = (int)std::floor(b.max[axis] + d); step = 1; }
    else { start = (int)std::floor(b.min[axis] + eps) - 1; end = (int)std::floor(b.min[axis] + d); step = -1; }
    for (int k = start; step > 0 ? k <= end : k >= end; k += step) {
        bool solid = false;
        for (int i = a0; i <= a1 && !solid; i++) for (int j = c0; j <= c1 && !solid; j++) {
            int c[3]; c[axis] = k; c[o1] = i; c[o2] = j;
            solid = c[1] < 0 || w.reg.solid(w.getBlock(c[0], c[1], c[2]));
        }
        if (solid) { allowed = d > 0 ? (float)k - b.max[axis] : (float)(k + 1) - b.min[axis]; hit = true; break; }
    }
    if (d > 0) allowed = std::max(0.0f, std::min(allowed, d)); else allowed = std::min(0.0f, std::max(allowed, d));
    pos[axis] += allowed;
}

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
    if (flying && onGround) flying = false;   // landing ends flight

    // fall distance: accumulated while descending freely, reported once on landing
    landedFall = -1;
    if (inFluid || onClimbable || flying) fallDistance = 0;
    else if (pos.y < oldY) fallDistance += oldY - pos.y;
    if (onGround && !wasGround) { landedFall = fallDistance; fallDistance = 0; }
}

RayHit raycastBlocks(const World& w, Vec3 o, Vec3 d, float reach) {
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
        if (s != AIR && !w.reg.fluid(s)) { r.block = {x, y, z}; r.normal = n; r.distance = t; r.hit = true; return r; }
        if (tmx < tmy && tmx < tmz) { x += stepX; t = tmx; tmx += tdx; n = {-stepX, 0, 0}; }
        else if (tmy < tmz) { y += stepY; t = tmy; tmy += tdy; n = {0, -stepY, 0}; }
        else { z += stepZ; t = tmz; tmz += tdz; n = {0, 0, -stepZ}; }
    }
    return r;
}

} // namespace vx
