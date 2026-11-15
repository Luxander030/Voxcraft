// Carts: entities that ride rails.
//
// On a rail a cart moves along the rail's centre line (railGeometry) with a signed speed s; when
// it runs past an end it continues into the neighbouring cell in that end's direction, keeping
// its speed (so curves cost nothing). Slopes add a gravity component, powered rails boost (or
// brake when unpowered), and friction slowly stops a coasting cart. Off rails it is a plain
// falling box. Positions are the cart's bottom centre. Carts persist with their chunk.
#pragma once
#include "game/rails.h"
#include "game/player.h"

namespace vx {

struct Cart {
    u32 id = 0;
    Vec3 pos, prevPos, vel;
    float yaw = 0, prevYaw = 0, pitch = 0, prevPitch = 0;
    int damage = 0, hurtTicks = 0;
    bool removed = false, onRail = false;
    AABB box() const { return {{pos.x - 0.49f, pos.y, pos.z - 0.49f}, {pos.x + 0.49f, pos.y + 0.7f, pos.z + 0.49f}}; }
};

class CartSystem {
public:
    static constexpr float MAX_SPEED = 0.4f, FRICTION = 0.997f, SLOPE_G = 0.012f, BOOST = 0.04f;
    std::vector<Cart> carts;
    Cart& spawn(Vec3 pos);
    void tick(const World& w);
    void step(const World& w, Cart& c);   // one tick of one cart (exposed for tests)
    Cart* raycast(Vec3 origin, Vec3 dir, float reach, float* dist = nullptr);
    Cart* byId(u32 id);
    u32 nextId = 1;
private:
    bool railCell(const World& w, Vec3 p, BlockPos& cell, BlockState& s) const;
};

} // namespace vx
