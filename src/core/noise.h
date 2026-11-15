// Deterministic gradient noise for world generation.
//
// Determinism rules (cross-platform, cross-architecture): lattice gradients come from integer
// hashing (no tables built from std::rand), only + - * / and floor are used (all IEEE-exact
// under -ffp-contract=off / MSVC /fp:precise), no transcendental library calls.
#pragma once
#include "core/common.h"

namespace vx {

class Noise {
public:
    explicit Noise(u64 seed = 0) : seed_(seed) {}
    float noise2(float x, float z) const;            // ~[-1, 1]
    float noise3(float x, float y, float z) const;   // ~[-1, 1]
    // Fractal sum, normalised to ~[-1,1].
    float fbm2(float x, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    float fbm3(float x, float y, float z, int octaves, float lacunarity = 2.0f, float gain = 0.5f) const;
    u64 seed() const { return seed_; }
private:
    u64 seed_;
};

} // namespace vx
