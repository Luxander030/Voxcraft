#include "game/projectiles.h"

namespace vx {

void ProjectileSystem::shoot(ProjKind k, Vec3 pos, Vec3 vel, float damage, bool fromPlayer, bool pickup) {
    Projectile p; p.kind = k; p.pos = p.prevPos = pos; p.vel = vel; p.damage = damage; p.fromPlayer = fromPlayer; p.pickup = pickup;
    list.push_back(p);
}

void ProjectileSystem::tick(const World& w, MobSystem& mobs, const Player& player, ProjectileHooks& hooks) {
    for (auto& p : list) {
        if (p.removed) continue;
        p.prevPos = p.pos;
        p.age++;
        if (p.stuck) {
            if (p.age > STUCK_LIFETIME) { p.removed = true; continue; }
            Vec3 d = p.pos - (player.pos + Vec3(0, 0.9f, 0));
            if (p.pickup && dot(d, d) < 1.6f * 1.6f && hooks.pickupArrow && hooks.pickupArrow()) p.removed = true;
            continue;
        }
        if (p.age > 400 || p.pos.y < -64) { p.removed = true; continue; }
        float speed = length(p.vel);
        if (speed < 1e-4f) { p.stuck = true; continue; }
        Vec3 dir = p.vel * (1.0f / speed);
        RayHit bh = raycastBlocks(w, p.pos, dir, speed);
        float limit = bh.hit ? bh.distance : speed;
        // creatures (player projectiles) or the player (creature projectiles)
        if (p.fromPlayer) {
            float md = 0;
            if (Mob* m = mobs.raycast(p.pos, dir, limit, &md)) {
                if (hooks.hitMob) hooks.hitMob(*m, p.kind == ProjKind::Arrow ? std::ceil(speed * 2.5f * p.damage / 2) : p.damage, dir);
                if (hooks.impact) hooks.impact(p);
                p.removed = true; continue;
            }
        } else {
            AABB b = player.box();
            float t0 = 0, t1 = limit; bool hit = true;
            for (int a = 0; a < 3 && hit; a++) {
                float inv = std::fabs(dir[a]) < 1e-9f ? 1e30f : 1.0f / dir[a];
                float ta = (b.min[a] - p.pos[a]) * inv, tb = (b.max[a] - p.pos[a]) * inv;
                if (ta > tb) std::swap(ta, tb);
                t0 = std::max(t0, ta); t1 = std::min(t1, tb); if (t0 > t1) hit = false;
            }
            if (hit) { if (hooks.hitPlayer) hooks.hitPlayer(p.damage, dir); if (hooks.impact) hooks.impact(p); p.removed = true; continue; }
        }
        if (bh.hit) {
            if (hooks.impact) hooks.impact(p);
            if (p.kind == ProjKind::EmberBolt) { p.removed = true; continue; }
            p.pos = p.pos + dir * std::max(0.0f, bh.distance - 0.05f);
            p.stuck = true; p.age = 0; p.vel = dir * 0.001f;   // keep orientation for rendering
            continue;
        }
        p.pos = p.pos + p.vel;
        if (p.kind == ProjKind::Arrow) { p.vel.y -= 0.05f; p.vel *= 0.99f; }
    }
    list.erase(std::remove_if(list.begin(), list.end(), [](const Projectile& p) { return p.removed; }), list.end());
}

} // namespace vx
