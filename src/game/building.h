// Building shapes: placement rules, fence/pane connections, doors and trapdoors.
//
// Shapes are pure functions of block state (see BlockRegistry::buildShapes), so connections are
// stored as state properties ("north", "south", "west", "east") and refreshed by neighbour
// updates, exactly like any other state change. That keeps meshing, collision and saves free of
// neighbour lookups. Kept in the game layer (not the app) so every rule is unit-testable.
#pragma once
#include "world/world.h"

namespace vx {

struct PlacePlan {
    bool ok = false;
    BlockPos at;            // where the (lower) block goes
    BlockState state = AIR;
    BlockState upper = AIR; // doors: the upper half at at + (0,1,0)
};

// Plans placing `item` (a building-shape block) against the clicked block. `normal` is the
// clicked face, `hit` the world-space hit point, `look` the player's view direction.
// Handles slab stacking (two slabs merge into a double slab), stair/trapdoor halves, facing,
// door hinges (pairs form double doors) and initial fence/pane connections.
PlacePlan planBuildingPlacement(const World& w, BlockState item, const BlockPos& clicked, const BlockPos& normal, Vec3 hit, Vec3 look);

// Writes a plan into the world. A door's upper half is written first without neighbour updates,
// then the lower half normally, so neither half ever sees itself without its partner.
bool applyPlacement(World& w, const PlacePlan& plan);

// Fence / pane connections recomputed from the neighbours (returns s unchanged for other blocks).
BlockState withConnections(const World& w, const BlockPos& p, BlockState s);

// Right-click on a door or trapdoor: toggles it (both door halves). Returns false if the block
// cannot be opened by hand (iron doors open only with a circuit signal) or is not openable.
bool toggleOpenable(World& w, const BlockPos& p);

// Neighbour update for building shapes: refresh connections, remove a door half whose partner
// is gone, follow circuit power. `power(p)` returns the signal (0..15) arriving at p.
// Returns true if the block was handled.
bool buildingNeighborUpdate(World& w, const BlockPos& p, const std::function<int(const BlockPos&)>& power);

bool isDoor(const BlockRegistry& reg, BlockState s);
bool isOpenable(const BlockRegistry& reg, BlockState s);

} // namespace vx
