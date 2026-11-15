#include "render/farterrain.h"

namespace vx {

namespace {
u32 pack(float r, float g, float b) { return (u32)(clamp(r, 0.f, 1.f) * 255) | ((u32)(clamp(g, 0.f, 1.f) * 255) << 8) | ((u32)(clamp(b, 0.f, 1.f) * 255) << 16) | 0xFF000000u; }
u32 colorFor(const ColumnInfo& c) {
    if (c.height < SEA_LEVEL) { float d = clamp((SEA_LEVEL - c.height) / 20.0f, 0.f, 1.f); return pack(0.18f - 0.08f * d, 0.32f - 0.12f * d, 0.70f - 0.2f * d); }
    if (c.height > 150) return pack(0.93f, 0.95f, 0.98f);                                   // snow caps
    switch (c.biome) {
        case Biome::Desert: case Biome::Beach: return pack(0.85f, 0.80f, 0.58f);
        case Biome::Badlands: return pack(0.70f, 0.42f, 0.24f);
        case Biome::Tundra: case Biome::SnowyPeaks: case Biome::FrozenOcean: return pack(0.90f, 0.93f, 0.96f);
        case Biome::Mountains: return c.height > 120 ? pack(0.50f, 0.50f, 0.52f) : pack(0.42f, 0.58f, 0.36f);
        case Biome::Forest: case Biome::BirchForest: return pack(0.26f, 0.45f, 0.20f);
        case Biome::Taiga: return pack(0.24f, 0.38f, 0.26f);
        case Biome::Swamp: return pack(0.30f, 0.36f, 0.20f);
        case Biome::Savanna: return pack(0.58f, 0.56f, 0.30f);
        default: return pack(0.40f, 0.60f, 0.28f);
    }
}
} // namespace

void buildFarTerrain(const WorldGenerator& gen, int cx, int cz, int inner, int outer, int cell, FarTerrainMesh& out, int maxQuads) {
    out.centerX = cx; out.centerZ = cz; out.verts.clear();
    int n = (2 * outer) / cell;
    // corner heights on a (n+1)^2 grid
    std::vector<float> h((size_t)(n + 1) * (n + 1));
    std::vector<u32> col((size_t)(n + 1) * (n + 1));
    int x0 = cx - outer, z0 = cz - outer;
    for (int j = 0; j <= n; j++) for (int i = 0; i <= n; i++) {
        ColumnInfo c = gen.column(x0 + i * cell, z0 + j * cell);
        float dx = (float)(i * cell - outer), dz = (float)(j * cell - outer), d = std::sqrt(dx * dx + dz * dz);
        // sit under the real surface; in the band overlapping the loaded chunks, sink further so
        // valleys and rivers of the real terrain are never covered by the coarse mesh
        float sink = d < (float)inner + 32 ? 8.0f : 1.5f;
        h[(size_t)j * (n + 1) + i] = (float)std::max(c.height, SEA_LEVEL) - sink;
        col[(size_t)j * (n + 1) + i] = colorFor(c);
    }
    float r2i = (float)(inner - cell) * (float)(inner - cell), r2o = (float)outer * (float)outer;
    out.verts.reserve((size_t)std::min(maxQuads, n * n) * 4);
    for (int j = 0; j < n; j++) for (int i = 0; i < n; i++) {
        float mx = (float)(x0 + i * cell + cell / 2 - cx), mz = (float)(z0 + j * cell + cell / 2 - cz);
        float d2 = mx * mx + mz * mz;
        if (d2 < r2i || d2 > r2o) continue;
        if (out.quads() >= maxQuads) return;
        auto H = [&](int a, int b) { return h[(size_t)(j + b) * (n + 1) + i + a]; };
        u32 c = col[(size_t)j * (n + 1) + i];
        float fx = (float)(x0 + i * cell - cx), fz = (float)(z0 + j * cell - cz), s = (float)cell;
        // counter-clockwise seen from above (+Y): (0,0) (0,1) (1,1) (1,0) in (x,z)
        out.verts.push_back({fx, H(0, 0), fz, c});
        out.verts.push_back({fx, H(0, 1), fz + s, c});
        out.verts.push_back({fx + s, H(1, 1), fz + s, c});
        out.verts.push_back({fx + s, H(1, 0), fz, c});
    }
}

} // namespace vx
