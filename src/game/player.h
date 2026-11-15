// Player movement, collision and block targeting. Fixed 20 Hz step, deterministic, headless.
//
// Collision: the player's AABB is swept one axis at a time (Y, then X, then Z) against the unit
// boxes of solid blocks, which is exact for axis-aligned voxel worlds and never tunnels at
// player speeds (max displacement per tick < 1 block). Feel constants are our own tuning and live
// in MoveTuning so they can be adjusted (and tested) in one place.
#pragma once
#include "core/common.h"
#include "world/world.h"

namespace vx {

struct MoveTuning {
    float walk = 0.215f, sprintMul = 1.3f, sneakMul = 0.3f;   // blocks per tick
    float groundGrip = 0.45f, airGrip = 0.08f;                 // fraction of velocity error corrected per tick
    float gravity = 0.078f, verticalDrag = 0.98f, jump = 0.415f, maxFall = 3.5f;
    float flySpeed = 0.55f, flyVertical = 0.38f;
    float swimGravity = 0.02f, swimDrag = 0.8f, swimUp = 0.045f;
    float climb = 0.12f, climbMaxFall = 0.15f;
    float width = 0.6f, height = 1.8f, sneakHeight = 1.5f, eye = 1.62f, sneakEye = 1.27f;
};

struct PlayerInput {
    float forward = 0, strafe = 0;      // -1..1 (strafe +1 = right)
    bool jump = false, sneak = false, sprint = false;
    bool toggleFly = false;
};

class Player {
public:
    Vec3 pos{0.5f, 80, 0.5f};    // feet, horizontal centre
    Vec3 vel;
    float yaw = 0, pitch = 0;      // radians; yaw 0 looks toward -Z, positive yaw turns right
    bool onGround = false, flying = false, sneaking = false, sprinting = false;
    bool inFluid = false, headInFluid = false, onClimbable = false;
    float fallDistance = 0;
    float landedFall = -1;        // >= 0 on the tick the player lands: distance fallen (gameplay applies damage)
    bool canFly = true;            // creative
    MoveTuning tune;

    void tick(const World& w, const PlayerInput& in);
    AABB box() const;
    AABB boxAt(const Vec3& p) const;
    float eyeHeight() const { return sneaking && !flying ? tune.sneakEye : tune.eye; }
    Vec3 eye() const { return {pos.x, pos.y + eyeHeight(), pos.z}; }
    Vec3 look() const;    // unit view direction
    // Last tick's displacement for interpolated rendering.
    Vec3 prevPos;

private:
    bool collides(const World& w, const AABB& b) const;
    void moveAxis(const World& w, int axis, float d, bool& hit);
};

// Exact axis sweep of an AABB against solid blocks: returns the allowed displacement (<= |d|).
float sweepAxis(const World& w, const AABB& box, int axis, float d, bool& hit);

// Voxel ray traversal (Amanatides-Woo). Returns the first targetable block within `reach`.
struct RayHit { BlockPos block; BlockPos normal; float distance = 0; bool hit = false; };
RayHit raycastBlocks(const World& w, Vec3 origin, Vec3 dir, float reach, bool hitFluidSources = false);

} // namespace vx
