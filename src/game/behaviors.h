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
    // Circuits: lever/button use, and the game's "is an entity standing on this plate" query.
    void toggleLever(const BlockPos& p);
    void pressButton(const BlockPos& p);
    void pressPlate(const BlockPos& p);
    std::function<bool(const BlockPos&)> entityOnPlate;
    // Detector rails: a cart is on the rail at p (switches it on); and the game's "is a cart still
    // on this rail" query for the scheduled switch-off.
    void cartOnDetector(const BlockPos& p);
    std::function<bool(const BlockPos&)> cartOnRail;
    // Hooks into the item layer (set by the game / tests):
    std::function<int(const BlockPos&)> containerSignal;                       // comparator input 0..15 (-1 = not a container)
    std::function<void(const BlockPos&, bool dispenser, const BlockPos& dir)> onDispense;
    void containerChanged(const BlockPos& p) { notifyAround(p); }             // container contents changed (comparators)
    void toggleComparator(const BlockPos& p);
    // Rifts (dimension gates): fill the slate frame around `inside` (either vertical plane).
    // Returns the number of rift blocks created (0 = not a valid closed frame).
    int lightRift(const BlockPos& inside);
    // Builds a complete frame (interior 2 wide x 3 tall) with its rift, interior bottom-left at `base`.
    void buildRiftGate(const BlockPos& base, bool alongX);
    // Signal strength (0..15) arriving at p from all six neighbours (exposed for tests/debug).
    int inputPower(const BlockPos& p) const;
private:
    void neighborChanged(const BlockPos& p, const BlockPos& from);
    void processUpdate(const BlockPos& p, const BlockPos& from);
    // circuits.cpp
    bool circuitUpdate(const BlockPos& p, BlockState s, const BlockPos& from);   // true if handled
    bool circuitTick(const BlockPos& p, BlockState s);
    int emitInto(const BlockPos& src, const BlockPos& dst, bool forWire) const;
    int strongPower(const BlockPos& b) const;       // solid block powered by a source/relay into it
    bool weaklyPowered(const BlockPos& b) const;    // solid block under a powered wire
    void notifyAround(const BlockPos& p);
    void pistonSet(const BlockPos& p, BlockState s, bool extend);
    bool inUpdate_ = false;
    std::vector<std::pair<BlockPos, BlockPos>> queue_;
    bool riftSupported(const BlockPos& p) const;
    u16 detectorIdx_ = 0xFFFF, riftIdx_ = 0xFFFF, slateIdx_ = 0xFFFF, stickyIdx_ = 0xFFFF, comparatorIdx_ = 0xFFFF, dropperIdx_ = 0xFFFF, dispenserIdx_ = 0xFFFF, hopperIdx_ = 0xFFFF;
    bool isFloorSource(u16 i) const { return i == leverIdx_ || i == buttonIdx_ || i == plateIdx_ || i == detectorIdx_; }
    bool isPiston(u16 i) const { return i == pistonIdx_ || i == stickyIdx_; }
    int comparatorTarget(const BlockPos& p, BlockState s) const;
    u16 wireIdx_, torchIdx_, leverIdx_, buttonIdx_, plateIdx_, lampIdx_, relayIdx_, watcherIdx_, pistonIdx_, headIdx_;
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
