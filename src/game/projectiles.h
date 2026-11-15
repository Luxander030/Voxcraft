// Projectiles: arrows (gravity, stick in blocks, can be picked up) and ember bolts (straight,
// burst on impact). Each tick the swept segment pos -> pos + vel is tested against blocks
// (voxel traversal), creatures (ray vs AABB) and the player, so fast projectiles never tunnel.
#pragma once
#include "game/mobs.h"

namespace vx {

enum class ProjKind : u8 { Arrow, EmberBolt };

struct Projectile {
    ProjKind kind = ProjKind::Arrow;
    Vec3 pos, prevPos, vel;
    float damage = 2;
    bool fromPlayer = true, stuck = false, pickup = true, removed = false;
    int age = 0;
};

struct ProjectileHooks {
    std::function<void(float dmg, Vec3 dir)> hitPlayer;
    std::function<void(Mob&, float dmg, Vec3 dir)> hitMob;
    std::function<bool()> pickupArrow;            // returns true if the arrow went into the inventory
    std::function<void(const Projectile&)> impact; // particles / sound
};

class ProjectileSystem {
public:
    std::vector<Projectile> list;
    static constexpr int STUCK_LIFETIME = 1200;   // 60 s
    void shoot(ProjKind k, Vec3 pos, Vec3 vel, float damage, bool fromPlayer, bool pickup = true);
    void tick(const World& w, MobSystem& mobs, const Player& player, ProjectileHooks& hooks);
};

} // namespace vx
