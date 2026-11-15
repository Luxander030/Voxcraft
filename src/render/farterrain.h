// Distant-terrain LOD: a coarse heightfield beyond the loaded render distance, built directly from
// the generator's column heights (no chunks are generated or loaded for it). 8-block cells with
// shared corner heights (no cracks), coloured by biome / altitude / water, placed slightly below
// the real surface so it never z-fights the loaded terrain where they overlap.
#pragma once
#include "world/worldgen.h"

namespace vx {

struct FarVertex { float x, y, z; u32 color; };   // position relative to the mesh centre

struct FarTerrainMesh {
    int centerX = 0, centerZ = 0;   // world coords (cell-aligned)
    std::vector<FarVertex> verts;   // quads, 4 vertices each
    int quads() const { return (int)verts.size() / 4; }
};

// Builds the ring between innerRadius and outerRadius (blocks) around (cx, cz). Thread-safe
// as long as the generator is (it is: generation is a pure function).
void buildFarTerrain(const WorldGenerator& gen, int cx, int cz, int innerRadius, int outerRadius, int cell, FarTerrainMesh& out, int maxQuads = 16000);

} // namespace vx
