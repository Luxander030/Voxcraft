#include "game/particles.h"

namespace vx {

void ParticleSystem::blockDebris(const BlockPos& b, u16 layer, u32 tint, int n, Rng& rng, Vec3 bias) {
    for (int i = 0; i < n; i++) {
        Particle p;
        p.pos = {b.x + 0.1f + rng.unit() * 0.8f, b.y + 0.1f + rng.unit() * 0.8f, b.z + 0.1f + rng.unit() * 0.8f};
        p.vel = Vec3((rng.unit() - 0.5f) * 0.15f, rng.unit() * 0.2f, (rng.unit() - 0.5f) * 0.15f) + bias;
        p.maxLife = p.life = 10 + rng.range(20);
        p.size = 0.08f + rng.unit() * 0.06f;
        p.layer = layer; p.u0 = rng.range(12) / 16.0f; p.v0 = rng.range(12) / 16.0f; p.uvSize = 4 / 16.0f; p.tint = tint;
        add(p);
    }
}

void ParticleSystem::puff(Vec3 at, int n, u32 tint, Rng& rng) {
    for (int i = 0; i < n; i++) {
        Particle p; p.pos = at + Vec3((rng.unit() - 0.5f) * 0.6f, rng.unit() * 0.8f, (rng.unit() - 0.5f) * 0.6f);
        p.vel = {(rng.unit() - 0.5f) * 0.06f, 0.02f + rng.unit() * 0.04f, (rng.unit() - 0.5f) * 0.06f};
        p.maxLife = p.life = 15 + rng.range(15); p.size = 0.15f + rng.unit() * 0.1f; p.gravity = -0.002f; p.collide = false;
        p.layer = whiteLayer; p.u0 = p.v0 = 0.5f; p.uvSize = 0; p.tint = tint;   // solid colour: one texel of a white tile
        add(p);
    }
}

void ParticleSystem::flame(Vec3 at, u16 layer, Rng& rng) {
    Particle p; p.pos = at + Vec3((rng.unit() - 0.5f) * 0.05f, 0, (rng.unit() - 0.5f) * 0.05f);
    p.vel = {0, 0.01f + rng.unit() * 0.01f, 0}; p.maxLife = p.life = 8 + rng.range(6);
    p.size = 0.06f; p.gravity = 0; p.collide = false; p.emissive = true;
    p.layer = layer; p.u0 = 7 / 16.0f; p.v0 = 4 / 16.0f; p.uvSize = 2 / 16.0f;
    add(p);
    if (rng.chance(0.3f)) {   // smoke
        Particle s = p; s.emissive = false; s.maxLife = s.life = 20 + rng.range(10); s.size = 0.09f; s.vel.y = 0.03f; s.uvSize = 0; s.tint = 0xC0505050u;
        s.layer = whiteLayer; s.u0 = s.v0 = 0.5f;
        s.pos.y += 0.1f; add(s);
    }
}

void ParticleSystem::splash(Vec3 at, u16 layer, u32 tint, Rng& rng) {
    for (int i = 0; i < 14; i++) {
        Particle p; p.pos = at + Vec3((rng.unit() - 0.5f) * 0.8f, 0.1f, (rng.unit() - 0.5f) * 0.8f);
        p.vel = {(rng.unit() - 0.5f) * 0.1f, 0.15f + rng.unit() * 0.15f, (rng.unit() - 0.5f) * 0.1f};
        p.maxLife = p.life = 10 + rng.range(10); p.size = 0.06f; p.layer = layer; p.uvSize = 2 / 16.0f; p.tint = tint;
        add(p);
    }
}

void ParticleSystem::tick(const World& w) {
    size_t out = 0;
    for (size_t i = 0; i < parts.size(); i++) {
        Particle p = parts[i];
        if (--p.life <= 0) continue;
        p.vel.y -= p.gravity; p.vel *= 0.98f;
        Vec3 np = p.pos + p.vel;
        if (p.collide && w.reg.solid(w.getBlock((int)std::floor(np.x), (int)std::floor(np.y), (int)std::floor(np.z)))) {
            if (w.reg.solid(w.getBlock((int)std::floor(p.pos.x), (int)std::floor(np.y), (int)std::floor(p.pos.z)))) { p.vel.y = 0; np.y = p.pos.y; }
            p.vel.x *= 0.5f; p.vel.z *= 0.5f; np.x = p.pos.x; np.z = p.pos.z;
        }
        p.pos = np;
        parts[out++] = p;   // compact in place, no allocation
    }
    parts.resize(out);
}

} // namespace vx
