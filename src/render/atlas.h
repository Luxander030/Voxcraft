// Texture pack (assets/generated/voxcraft.pak, written by tools/build_assets.py) and the
// block-face -> texture-layer table used by the mesher. CPU only; the GPU upload lives in src/app.
//
// All block textures are layers of one 16x16 2D array texture: no atlas bleeding, no UV padding,
// one bind for all terrain, and greedy-merged quads can tile with REPEAT addressing.
#pragma once
#include "core/common.h"
#include "world/block.h"
#include <string>
#include <unordered_map>

namespace vx {

struct TexturePack {
    int tileSize = 16;
    int tileCount = 0;
    std::vector<u8> rgba;                       // tileCount * tileSize^2 * 4
    std::vector<u8> flags;                      // per tile: 1 = has transparent pixels, 2 = partial alpha
    std::unordered_map<std::string, u16> names; // texture name -> tile (deduplicated aliases share tiles)

    bool load(const std::string& path, std::string* err);
    bool parse(const std::vector<u8>& bytes, std::string* err);
    int layer(const std::string& name) const { auto it = names.find(name); return it == names.end() ? -1 : it->second; }
    // Complete mip chain, all levels tightly packed (level 0 first, every level holds all layers).
    // Alpha-tested tiles keep their coverage by using max-alpha with colour from opaque texels.
    std::vector<u8> buildMips(int& levels) const;
};

// Per block state: texture layer for each face (after facing/lit variants) - resolved once.
class BlockTextures {
public:
    bool build(const BlockRegistry& reg, const TexturePack& pack, std::string* err);
    u16 face(BlockState s, int face) const { return faces_[(size_t)s * 6 + face]; }
    u16 missing() const { return missing_; }
private:
    std::vector<u16> faces_;
    u16 missing_ = 0;
};

} // namespace vx
