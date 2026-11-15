// Particles: short-lived billboards (block debris, dust, flames, smoke, splashes).
// Pooled in a fixed-capacity vector (no per-particle allocation); simple gravity + block
// collision; drawn as camera-facing quads in the entity pass.
#pragma once
#include "world/world.h"

namespace vx {

struct Particle {
    Vec3 pos, vel;
    float life = 0, maxLife = 1, size = 0.1f, gravity = 0.04f;
    u16 layer = 0; float u0 = 0, v0 = 0, uvSize = 0.25f;   // sub-rectangle of the tile (tile units)
    u32 tint = 0xFFFFFFFFu;
    bool collide = true, emissive = false;
};

class ParticleSystem {
public:
    static constexpr size_t CAPACITY = 4000;
    std::vector<Particle> parts;
    u16 whiteLayer = 0;   // near-white tile used for untextured (tinted) particles
    ParticleSystem() { parts.reserve(CAPACITY); }
    void add(const Particle& p) { if (parts.size() < CAPACITY) parts.push_back(p); }
    // Debris from a block: `n` particles sampling random 4x4 texel regions of its texture.
    void blockDebris(const BlockPos& b, u16 layer, u32 tint, int n, Rng& rng, Vec3 bias = {});
    void puff(Vec3 at, int n, u32 tint, Rng& rng);
    void flame(Vec3 at, u16 layer, Rng& rng);
    void splash(Vec3 at, u16 layer, u32 tint, Rng& rng);
    void tick(const World& w);   // 20 Hz
};

} // namespace vx
