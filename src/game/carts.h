// Carts: entities that ride rails.
//
// On a rail a cart moves along the rail's centre line (railGeometry) with a signed speed s; when
// it runs past an end it continues into the neighbouring cell in that end's direction, keeping
// its speed (so curves cost nothing). Slopes add a gravity component, powered rails boost (or
// brake when unpowered), and friction slowly stops a coasting cart. Off rails it is a plain
// falling box. Positions are the cart's bottom centre. Carts persist with their chunk.
// A storage cart carries a 27-slot chest that players open and hoppers fill or empty.
#pragma once
#include "game/rails.h"
#include "game/player.h"
#include "game/items.h"

namespace vx {
class MobSystem;

enum class CartKind : u8 { Plain = 0, Storage = 1 };

struct Cart {
    u32 id = 0;
    CartKind kind = CartKind::Plain;
    std::vector<ItemStack> items;   // storage carts: STORAGE_SLOTS slots
    Vec3 pos, prevPos, vel;
    Vec3 push;   // impulses from collisions this tick; on a rail only the along-track part counts
    float yaw = 0, prevYaw = 0, pitch = 0, prevPitch = 0;
    int damage = 0, hurtTicks = 0;
    bool removed = false, onRail = false;
    AABB box() const { return {{pos.x - 0.49f, pos.y, pos.z - 0.49f}, {pos.x + 0.49f, pos.y + 0.7f, pos.z + 0.49f}}; }
};

class CartSystem {
public:
    static constexpr float MAX_SPEED = 0.4f, FRICTION = 0.997f, SLOPE_G = 0.012f, BOOST = 0.04f;
    static constexpr int STORAGE_SLOTS = 27;
    std::vector<Cart> carts;
    Cart& spawn(Vec3 pos, CartKind kind = CartKind::Plain);
    // The storage cart whose box reaches into block cell p (hoppers above/below a track), or null.
    Cart* storageAt(const BlockPos& p);
    void tick(const World& w);
    void step(const World& w, Cart& c);   // one tick of one cart (exposed for tests)
    Cart* raycast(Vec3 origin, Vec3 dir, float reach, float* dist = nullptr);
    Cart* byId(u32 id);
    u32 nextId = 1;
    // Called after each cart's step with the rail cell it occupies (detector rails).
    std::function<void(const BlockPos&, BlockState)> onRail;
    // The rail cell a cart at p rides in (the top end of a slope belongs to the cell below).
    bool railCell(const World& w, Vec3 p, BlockPos& cell, BlockState& s) const;
    // Cart-to-cart contacts: equal masses exchange their approach velocity along the line between
    // centres (a moving cart hands its speed to one at rest) and overlapping carts drift apart.
    void collideCarts();
    // A body (creature, player) overlapping cart c. Returns false without contact; otherwise sets
    // `normal` (horizontal, cart -> body) and `approach` (closing speed, > 0 when the cart runs
    // into the body). The caller decides who gets pushed.
    static bool contact(const Cart& c, const AABB& body, Vec3 bodyVel, Vec3& normal, float& approach);
    static constexpr float CREATURE_MASS = 0.5f;   // a creature takes this share of a collision
};

// What a broken cart drops: the cart item (storage carts: the storage cart item) unless in creative,
// and always a storage cart's contents.
std::vector<ItemStack> cartDrops(const Cart& c, const ItemRegistry& items, bool creative);

// Carts against creatures: a cart running into a creature knocks it aside (the creature takes the
// closing speed and the cart loses CREATURE_MASS/2 of it), and overlapping bodies drift apart.
void collideCartsWithMobs(CartSystem& carts, MobSystem& mobs);
// The player walking into a cart (not the ridden one) nudges it along the track; a cart running
// into the player shoves them aside. Returns the push for the player.
Vec3 collideCartsWithPlayer(CartSystem& carts, const AABB& player, Vec3 playerVel, u32 ridden);

} // namespace vx
