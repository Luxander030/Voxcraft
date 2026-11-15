#include "render/mesher.h"
#include "world/world.h"

namespace vx {
bool buildMeshSnapshot(const World& w, ChunkPos c, MeshSnapshot& out) {
    out.center = c;
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) {
        ChunkPtr ch = w.chunk({c.x + dx, c.z + dz});
        if (!ch) return false;
        int i = MeshSnapshot::idx(dx, dz);
        for (int s = 0; s < SECTIONS; s++) { out.sec[i][s] = ch->sec[s]; out.light[i][s] = ch->light[s]; }
        std::memcpy(out.biome[i], ch->biome, 256);
    }
    return true;
}
} // namespace vx
