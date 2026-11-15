// Dropped item entities: physics, merging, despawn, pickup, and per-chunk persistence through
// World's EntityHost interface (entities are saved with the chunk they are in, and unload/reload
// with it, so they never accumulate in memory for chunks far away).
#pragma once
#include "game/items.h"
#include "game/mobs.h"
#include "game/player.h"
#include "game/carts.h"

namespace vx {

struct ItemEntity {
    Vec3 pos, vel;
    ItemStack stack;
    int age = 0, pickupDelay = 10;
    bool removed = false;
};

class EntityManager final : public EntityHost {
public:
    EntityManager(const ItemRegistry& items) : items_(items) {}
    std::vector<ItemEntity> items;
    static constexpr int DESPAWN_TICKS = 6000;   // 5 minutes

    void spawnItem(Vec3 pos, ItemStack s, Rng& rng, Vec3 throwVel = {});
    // Simulates entities in loaded chunks; picks up into `inv` when the player is close.
    void tick(const World& w, const Player& player, Inventory* inv);
    size_t count() const { return items.size(); }

    // EntityHost
    std::vector<EntityRecord> collectEntities(ChunkPos p, bool removeFromWorld) override;
    void spawnEntities(ChunkPos p, std::vector<EntityRecord>&& recs) override;
    std::function<void(const ItemStack&)> onPickup;   // UI/audio hook
    MobSystem* mobs = nullptr;                        // mobs persist through the same records
    CartSystem* carts = nullptr;                      // and carts
private:
    const ItemRegistry& items_;
};

} // namespace vx
