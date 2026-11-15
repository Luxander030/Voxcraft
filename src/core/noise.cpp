#include "core/noise.h"

namespace vx {

namespace {
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline int fl(float v) { int i = (int)v; return v < (float)i ? i - 1 : i; }

// 12 edge gradients for 3D, 8 for 2D (chosen by hash)
inline float grad2(u64 h, float x, float z) {
    switch (h & 7) {
        case 0: return x + z; case 1: return x - z; case 2: return -x + z; case 3: return -x - z;
        case 4: return x; case 5: return -x; case 6: return z; default: return -z;
    }
}
inline float grad3(u64 h, float x, float y, float z) {
    switch (h % 12) {
        case 0: return x + y; case 1: return -x + y; case 2: return x - y; case 3: return -x - y;
        case 4: return x + z; case 5: return -x + z; case 6: return x - z; case 7: return -x - z;
        case 8: return y + z; case 9: return -y + z; case 10: return y - z; default: return -y - z;
    }
}
} // namespace

float Noise::noise2(float x, float z) const {
    int xi = fl(x), zi = fl(z);
    float xf = x - (float)xi, zf = z - (float)zi;
    float u = fade(xf), v = fade(zf);
    float a = grad2(hashCoords(seed_, xi, zi), xf, zf);
    float b = grad2(hashCoords(seed_, xi + 1, zi), xf - 1, zf);
    float c = grad2(hashCoords(seed_, xi, zi + 1), xf, zf - 1);
    float d = grad2(hashCoords(seed_, xi + 1, zi + 1), xf - 1, zf - 1);
    return lerp(lerp(a, b, u), lerp(c, d, u), v) * 0.7071f;
}

float Noise::noise3(float x, float y, float z) const {
    int xi = fl(x), yi = fl(y), zi = fl(z);
    float xf = x - (float)xi, yf = y - (float)yi, zf = z - (float)zi;
    float u = fade(xf), v = fade(yf), w = fade(zf);
    auto g = [&](int dx, int dy, int dz) { return grad3(hashCoords(seed_, xi + dx, yi + dy, zi + dz), xf - (float)dx, yf - (float)dy, zf - (float)dz); };
    float x00 = lerp(g(0, 0, 0), g(1, 0, 0), u), x10 = lerp(g(0, 1, 0), g(1, 1, 0), u);
    float x01 = lerp(g(0, 0, 1), g(1, 0, 1), u), x11 = lerp(g(0, 1, 1), g(1, 1, 1), u);
    return lerp(lerp(x00, x10, v), lerp(x01, x11, v), w);
}

float Noise::fbm2(float x, float z, int octaves, float lac, float gain) const {
    float sum = 0, amp = 1, norm = 0, f = 1;
    for (int i = 0; i < octaves; i++) {
        Noise o(mix64(seed_ + (u64)i * 0x51ED27u));
        sum += o.noise2(x * f, z * f) * amp; norm += amp; amp *= gain; f *= lac;
    }
    return sum / norm;
}

float Noise::fbm3(float x, float y, float z, int octaves, float lac, float gain) const {
    float sum = 0, amp = 1, norm = 0, f = 1;
    for (int i = 0; i < octaves; i++) {
        Noise o(mix64(seed_ + (u64)i * 0x51ED27u));
        sum += o.noise3(x * f, y * f, z * f) * amp; norm += amp; amp *= gain; f *= lac;
    }
    return sum / norm;
}

} // namespace vx
