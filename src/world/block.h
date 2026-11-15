// Data-driven block registry (assets/data/blocks.json).
//
// A BlockState is a 16-bit id. Each block type owns a contiguous range of states, one per
// combination of its properties ("flattened" states). Hot-path properties (opaque, solid, light)
// are stored in flat per-state arrays so lookups are a single indexed load.
//
// State ids are assigned at load time and are NOT stable across versions; persistent data stores
// block names + property values through a palette (see storage/region.cpp).
#pragma once
#include "core/common.h"
#include "core/json.h"
#include <string>
#include <unordered_map>

namespace vx {

using BlockState = u16;
constexpr BlockState AIR = 0;

enum class BlockShape : u8 { None, Cube, Cross, Fluid, Torch, FlatWall, Wire, Lever, Button, Plate, SlabThin, PistonHead, Model };
enum class RenderLayer : u8 { Opaque, Cutout, Translucent };
enum class ToolType : u8 { None, Pickaxe, Axe, Shovel, Hoe, Sword };
enum class Tint : u8 { None, Grass, Foliage, Water };
enum Face : int { FACE_NX = 0, FACE_PX, FACE_NY, FACE_PY, FACE_NZ, FACE_PZ };

// block flags
enum : u32 {
    BF_REPLACEABLE = 1 << 0, BF_GRAVITY = 1 << 1, BF_FLUID = 1 << 2, BF_LEAVES = 1 << 3, BF_FLAMMABLE = 1 << 4,
    BF_NEEDS_SOIL = 1 << 5, BF_BLOCK_ENTITY = 1 << 6, BF_CLIMBABLE = 1 << 7, BF_ATTACHED = 1 << 8, BF_ATTACHED_FLOOR = 1 << 9,
    BF_CIRCUIT = 1 << 10, BF_SPREADS = 1 << 11, BF_SLIPPERY = 1 << 12, BF_HOT = 1 << 13, BF_PRICKLY = 1 << 14,
    BF_INTERACT_CRAFT = 1 << 15, BF_INTERACT_CHEST = 1 << 16, BF_INTERACT_KILN = 1 << 17,
};

struct BlockProp { std::string name; u8 count; u16 stride; };

struct BlockDef {
    std::string name;
    u16 index = 0;            // block type index
    BlockState base = 0;      // first state id
    u16 stateCount = 1;
    std::vector<BlockProp> props;
    BlockShape shape = BlockShape::Cube;
    RenderLayer layer = RenderLayer::Opaque;
    Tint tint = Tint::None;
    bool solid = true, opaque = true;
    u8 light = 0, opacity = 15;
    float hardness = 1.0f, resistance = 1.0f;
    ToolType tool = ToolType::None;
    u8 toolLevel = 0;
    std::string drop;          // item name; empty = nothing
    std::string sound = "stone";
    u32 flags = 0;
    std::string tex[6];        // per face (NX,PX,NY,PY,NZ,PZ); "front" maps onto the facing side at mesh time
    std::string texFront;      // optional
    bool has(u32 f) const { return (flags & f) != 0; }
    int propIndex(const char* p) const { for (size_t i = 0; i < props.size(); i++) if (props[i].name == p) return (int)i; return -1; }
};

class BlockRegistry {
public:
    bool load(const Json& root, std::string* err);
    bool loadFile(const std::string& path, std::string* err);

    const BlockDef& def(BlockState s) const { return defs_[stateBlock_[s]]; }
    const BlockDef* byName(const std::string& name) const;
    BlockState stateOf(const std::string& name) const { auto* d = byName(name); return d ? d->base : AIR; }
    size_t stateCount() const { return stateBlock_.size(); }
    size_t blockCount() const { return defs_.size(); }
    const std::vector<BlockDef>& defs() const { return defs_; }

    // properties
    int getProp(BlockState s, int propIdx) const { const BlockDef& d = def(s); if (propIdx < 0) return 0; const BlockProp& p = d.props[propIdx]; return ((s - d.base) / p.stride) % p.count; }
    int getProp(BlockState s, const char* name) const { return getProp(s, def(s).propIndex(name)); }
    BlockState withProp(BlockState s, int propIdx, int v) const {
        const BlockDef& d = def(s); if (propIdx < 0) return s; const BlockProp& p = d.props[propIdx];
        int cur = ((s - d.base) / p.stride) % p.count; return (BlockState)(s + (v - cur) * p.stride);
    }
    BlockState withProp(BlockState s, const char* name, int v) const { return withProp(s, def(s).propIndex(name), v); }
    std::string stateString(BlockState s) const;          // "name[prop=v,...]"
    BlockState parseState(const std::string& str) const;  // inverse; unknown -> AIR

    // hot-path tables
    bool opaque(BlockState s) const { return opaque_[s] != 0; }
    bool solid(BlockState s) const { return solid_[s] != 0; }
    u8 light(BlockState s) const { return light_[s]; }
    u8 opacity(BlockState s) const { return opacity_[s]; }
    bool isAir(BlockState s) const { return s == AIR; }
    bool fluid(BlockState s) const { return fluid_[s] != 0; }

private:
    std::vector<BlockDef> defs_;
    std::vector<u16> stateBlock_;
    std::vector<u8> opaque_, solid_, light_, opacity_, fluid_;
    std::unordered_map<std::string, u16> byName_;
};

// Commonly referenced states resolved once after load (world gen, fluids, circuits).
struct CoreBlocks {
    BlockState air, stone, cobblestone, slate, bedrock, dirt, turf, podzol, sand, red_sand, gravel, sandstone, clay,
        snow, ice, water, lava, oak_log, birch_log, pine_log, oak_leaves, birch_leaves, pine_leaves, cactus,
        tall_grass, fern, dead_bush, sunpetal, bluebell, emberbloom, coal_ore, copper_ore, iron_ore, gold_ore,
        glimmer_ore, azurite_ore, glowcrystal, chest, torch, wire, spark_torch, lever, button, plate, lamp, relay, watcher,
        piston, piston_head, workbench, kiln, glass, oak_planks;
    void resolve(const BlockRegistry& r);
};

} // namespace vx
