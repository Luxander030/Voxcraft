// Container block entities and the automation that moves items between them:
// kilns (smelting), hoppers (push/pull every 8 ticks, vacuum items above, locked while powered),
// droppers/dispensers (fired by circuits), and the comparator fullness signal.
#pragma once
#include "game/behaviors.h"
#include "game/entities.h"
#include "game/kiln.h"

namespace vx {

constexpr int HOPPER_SLOTS = 5, DROPPER_SLOTS = 9, HOPPER_COOLDOWN = 8;

struct ContainerContext {
    World& world;
    const ItemRegistry& items;
    const RecipeBook& recipes;
    BlockBehaviors* behaviors = nullptr;   // hopper locking, comparator notifications, fluids
    EntityManager* entities = nullptr;     // dropped items (hopper vacuum, droppers)
    Rng* rng = nullptr;
    std::function<void(Vec3 pos, Vec3 vel)> shootArrow;   // dispensers with arrows
};

// First access to a chest that is part of the generated terrain rolls its structure loot.
void rollGeneratedChestLoot(World& w, const RecipeBook& rb, const BlockPos& p, BlockEntity& be);
// Slot count for a container block (0 = not a container).
int containerSlots(const BlockRegistry& reg, BlockState s);
// Comparator signal 0..15 for the container at p, or -1 if p is not a container.
int containerSignal(ContainerContext& c, const BlockPos& p);
// Ticks kilns and hoppers in loaded chunks within `radius` chunks of `center`.
void tickContainers(ContainerContext& c, ChunkPos center, int radius);
// A dropper/dispenser at p fires once toward `dir`.
void dispense(ContainerContext& c, const BlockPos& p, bool dispenser, const BlockPos& dir);

} // namespace vx
