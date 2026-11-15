// Block behaviours driven by World's scheduled ticks, neighbour updates and random ticks:
// fluid flow, falling blocks, turf spread/decay, sapling growth.
//
// Everything is event driven: nothing scans the world per frame. A fluid only does work when it
// or a neighbour changes, so a still ocean costs nothing; flow advances one block per fluid tick
// (water 5 ticks, lava 30). Scheduled ticks are persistent chunk data, so a flow interrupted by a
// save/unload resumes exactly where it stopped.
//
// Fluid levels (block property "level"): 0 source, 1..7 flowing (distance from source, step 1 for
// water / 2 for lava), 8 falling (fed from above).
#pragma once
#include "world/world.h"

namespace vx {

class BlockBehaviors {
public:
    explicit BlockBehaviors(World& w);
    ~BlockBehaviors();
    static constexpr int WATER_DELAY = 5, LAVA_DELAY = 30, FALL_DELAY = 2;
    // Places a fluid source and schedules its first flow (used by buckets later, and tests).
    void placeFluidSource(const BlockPos& p, bool lava);
private:
    void neighborChanged(const BlockPos& p);
    void scheduled(const BlockPos& p, BlockState s);
    void randomTick(const BlockPos& p, BlockState s);
    void fluidTick(const BlockPos& p, BlockState s);
    void fallTick(const BlockPos& p, BlockState s);
    bool isFluid(BlockState s, u16 idx) const { return s != AIR && w_.reg.def(s).index == idx; }
    int level(BlockState s) const { return w_.reg.getProp(s, lvlProp_); }
    bool canFlowInto(BlockState s, u16 fluidIdx) const;
    void growTree(const BlockPos& p);
    World& w_;
    CoreBlocks cb;
    u16 waterIdx_, lavaIdx_;
    int lvlProp_;
};

} // namespace vx
