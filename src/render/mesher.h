// Section mesher: turns one 16^3 section (plus a 1-block border from its neighbours) into packed
// GPU vertices. Pure CPU, runs on worker threads, no GPU or World access.
//
// Vertex = 3 x u32 (12 bytes):
//   w0: x9 | y9<<9 | z9<<18 | face3<<27 | ao2<<30          position in 1/16 block units inside the section
//   w1: u9 | v9<<9 | layer10<<18 | flags4<<28              texture coords in 1/16 tile units (repeat), array layer
//   w2: sky4 | block4<<4 | tint RGB 8:8:8 << 8             smooth light per vertex, biome/tint colour
// Quads are 4 consecutive vertices; one shared index buffer (0,1,2, 0,2,3 per quad) serves every mesh.
//
// Optimisations: hidden-face culling against the padded neighbourhood, greedy merging of coplanar
// faces with identical texture/tint/light/AO (merged quads tile via REPEAT addressing), triangle
// diagonal chosen per quad from AO to avoid lighting anisotropy, uniform-air and fully-buried
// sections are rejected before any work.
//
// Block-state conventions used by special shapes:
//   4-way facing: 0 N(-Z) 1 S(+Z) 2 W(-X) 3 E(+X)      6-way facing: 0 down 1 up 2 N 3 S 4 W 5 E
//   torch facing 0..3: on the wall, pointing that direction; 4: standing on the floor
//   fluid level: 0 source, 1..7 flowing (lower), >= 8 falling (full height)
#pragma once
#include "core/common.h"
#include "render/atlas.h"
#include "world/chunk.h"

namespace vx {

enum MeshLayer { ML_OPAQUE = 0, ML_CUTOUT = 1, ML_TRANSLUCENT = 2, ML_COUNT = 3 };

struct SectionMesh {
    std::vector<u32> data[ML_COUNT];   // 3 u32 per vertex, 4 vertices per quad
    int quads(int l) const { return (int)(data[l].size() / 12); }
    int totalQuads() const { return quads(0) + quads(1) + quads(2); }
    bool empty() const { return data[0].empty() && data[1].empty() && data[2].empty(); }
    size_t bytes() const { return (data[0].size() + data[1].size() + data[2].size()) * 4; }
};

// Immutable snapshot of a 3x3 chunk neighbourhood (sections and light are shared copy-on-write).
struct MeshSnapshot {
    ChunkPos center;
    Section sec[9][SECTIONS];         // [ (dz+1)*3 + (dx+1) ][sy]
    LightSection light[9][SECTIONS];
    u8 biome[9][256];                 // copied: 2.3 KiB
    static int idx(int dx, int dz) { return (dz + 1) * 3 + (dx + 1); }
};

struct MeshContext {
    const BlockRegistry* reg = nullptr;
    const BlockTextures* tex = nullptr;
    bool greedy = true;
    bool smoothLight = true;
};

// True when the section has nothing visible at all (empty, or uniform opaque with opaque neighbours).
bool sectionTriviallyEmpty(const MeshSnapshot& s, int sy, const BlockRegistry& reg);
void meshSection(const MeshSnapshot& s, int sy, const MeshContext& ctx, SectionMesh& out);

// Decoding helpers (tests, debug tools)
struct UnpackedVertex { int x, y, z, face, ao, u, v, layer, flags, sky, block; u32 tint; };
inline UnpackedVertex unpackVertex(const u32* w) {
    return {(int)(w[0] & 511), (int)((w[0] >> 9) & 511), (int)((w[0] >> 18) & 511), (int)((w[0] >> 27) & 7), (int)(w[0] >> 30),
            (int)(w[1] & 511), (int)((w[1] >> 9) & 511), (int)((w[1] >> 18) & 1023), (int)(w[1] >> 28),
            (int)(w[2] & 15), (int)((w[2] >> 4) & 15), w[2] >> 8};
}

} // namespace vx

namespace vx {
class World;
// Snapshot the 3x3 neighbourhood of `center` from a World (main thread). False if any of the nine
// chunks is not loaded yet.
bool buildMeshSnapshot(const World& w, ChunkPos center, MeshSnapshot& out);
}
