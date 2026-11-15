// Deterministic, versioned world generation.
//
// generate(chunk) is a pure function of (generator version, seed, settings, dimension, chunk pos):
// it never reads other chunks or global mutable state, so any chunk can be regenerated at any
// time, in any order, on any thread, on any platform, bit-identically. Cross-chunk features
// (trees, ruins) are placed by evaluating candidate features of neighbouring chunks too.
//
// Old generator versions must be kept forever (or the world refuses to load) so existing
// worlds never get different terrain after an update; see ARCHITECTURE.md "World generation".
#pragma once
#include "core/common.h"
#include "core/noise.h"
#include "world/chunk.h"
#include <memory>

namespace vx {

enum class Biome : u8 {
    Ocean, DeepOcean, Beach, Plains, Forest, BirchForest, Taiga, Tundra, Desert, Badlands, Savanna, Swamp, Mountains, SnowyPeaks, River, FrozenOcean, Hollow,
    Count
};
const char* biomeName(Biome b);
struct BiomeColors { u32 grass, foliage, water; };
BiomeColors biomeColors(Biome b);

struct GenSettings {
    u64 seed = 0;
    u32 version = 1;
    std::string dimension = "overworld";
    std::string preset = "default";    // "default" | "flat"
    float terrainScale = 1.0f;          // horizontal stretch
    bool caves = true, ores = true, trees = true, structures = true;
};

struct ColumnInfo { int height; Biome biome; bool river; };

class WorldGenerator {
public:
    virtual ~WorldGenerator() = default;
    virtual void generate(Chunk& c) const = 0;
    virtual ColumnInfo column(int x, int z) const = 0;     // surface height + biome at world column
    virtual u32 version() const = 0;
    // Nearest village centre within `radiusRegions` 128-block regions (false if none / no villages).
    virtual bool nearestVillage(int x, int z, int radiusRegions, int& vx, int& vy, int& vz) const { (void)x; (void)z; (void)radiusRegions; (void)vx; (void)vy; (void)vz; return false; }
};

// Returns nullptr if the version is unknown (caller must refuse to open the world).
std::unique_ptr<WorldGenerator> createGenerator(const GenSettings& s, const BlockRegistry& reg);
// Version history: 1 = initial terrain, ruins, trees.  2 = adds watchtowers, buried caches, desert
// wells (and the Hollow dimension).  3 = adds villages.  4 = biome features (boulders, lily pads,
// capshrooms, ice spires, flower meadows).
constexpr u32 LATEST_GENERATOR_VERSION = 5;

} // namespace vx
