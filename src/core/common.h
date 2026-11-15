// Core types, small math, hashing. Header-only, no platform dependencies.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace vx {

using u8 = uint8_t;   using i8 = int8_t;
using u16 = uint16_t; using i16 = int16_t;
using u32 = uint32_t; using i32 = int32_t;
using u64 = uint64_t; using i64 = int64_t;

constexpr int CHUNK_W = 16;          // x/z extent of a chunk column
constexpr int SECTION_H = 16;        // y extent of one section
constexpr int WORLD_H = 256;         // y in [0, 256)
constexpr int SECTIONS = WORLD_H / SECTION_H;
constexpr int SEA_LEVEL = 63;

inline int floorDiv(int a, int b) { int q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
inline int floorMod(int a, int b) { int m = a % b; if (m != 0 && ((m < 0) != (b < 0))) m += b; return m; }
template <class T> inline T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct ChunkPos {
    i32 x = 0, z = 0;
    bool operator==(const ChunkPos& o) const { return x == o.x && z == o.z; }
    bool operator!=(const ChunkPos& o) const { return !(*this == o); }
};
struct ChunkPosHash { size_t operator()(const ChunkPos& p) const { return (size_t)((u64)(u32)p.x * 0x9E3779B97F4A7C15ull ^ (u64)(u32)p.z * 0xC2B2AE3D27D4EB4Full); } };

struct BlockPos {
    i32 x = 0, y = 0, z = 0;
    bool operator==(const BlockPos& o) const { return x == o.x && y == o.y && z == o.z; }
    BlockPos operator+(const BlockPos& o) const { return {x + o.x, y + o.y, z + o.z}; }
    ChunkPos chunk() const { return {x >> 4, z >> 4}; }
};
struct BlockPosHash { size_t operator()(const BlockPos& p) const { return (size_t)(((u64)(u32)p.x * 73856093ull) ^ ((u64)(u32)p.y * 19349663ull) ^ ((u64)(u32)p.z * 83492791ull)); } };

// Index of a block inside a chunk column: 4 bits x, 4 bits z, 8 bits y -> exactly 16 bits.
inline u16 columnIndex(int lx, int y, int lz) { return (u16)((y << 8) | (lz << 4) | lx); }
inline void columnIndexDecode(u16 i, int& lx, int& y, int& lz) { lx = i & 15; lz = (i >> 4) & 15; y = i >> 8; }

// ---- 64-bit mixing for deterministic hashing (splitmix64 finalizer) ----
inline u64 mix64(u64 z) { z += 0x9E3779B97F4A7C15ull; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
inline u64 hashCoords(u64 seed, i64 a, i64 b = 0, i64 c = 0) { return mix64(seed ^ mix64((u64)a ^ mix64((u64)b ^ mix64((u64)c)))); }

// Deterministic PRNG (xoshiro-like via splitmix). Same sequence on every platform.
struct Rng {
    u64 s;
    explicit Rng(u64 seed = 1) : s(seed) {}
    u64 next() { s += 0x9E3779B97F4A7C15ull; u64 z = s; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    u32 nextU32() { return (u32)(next() >> 32); }
    int range(int n) { return n <= 0 ? 0 : (int)((next() >> 33) % (u64)n); }   // [0,n)
    float unit() { return (float)(next() >> 40) * (1.0f / 16777216.0f); }      // [0,1)
    bool chance(float p) { return unit() < p; }
};

// FNV-1a 64 for content hashing (assets, names).
inline u64 fnv1a(const void* data, size_t n, u64 h = 0xcbf29ce484222325ull) {
    const u8* p = (const u8*)data; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; } return h;
}
inline u64 fnv1a(const std::string& s) { return fnv1a(s.data(), s.size()); }

// ---------------------------------------------------------------- math
struct Vec2 { float x = 0, y = 0; };
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default; Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const Vec3& a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3& a) { float l = length(a); return l > 1e-12f ? a * (1.0f / l) : Vec3(0, 0, 0); }
struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };

// Column-major 4x4 matrix (m[col*4+row]); clip-space depth in [0,1] (Metal / D3D convention).
struct Mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static Mat4 identity() { return {}; }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
            float s = 0; for (int k = 0; k < 4; k++) s += m[k * 4 + rr] * b.m[c * 4 + k]; r.m[c * 4 + rr] = s; }
        return r;
    }
    Vec4 operator*(const Vec4& v) const {
        return {m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12]*v.w, m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13]*v.w,
                m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w, m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w};
    }
    static Mat4 translate(float x, float y, float z) { Mat4 r; r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
    static Mat4 scale(float x, float y, float z) { Mat4 r; r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
    static Mat4 rotateX(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
    static Mat4 rotateY(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
    static Mat4 rotateZ(float a) { Mat4 r; float c = std::cos(a), s = std::sin(a); r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c; return r; }
    // Right-handed view, perspective with depth [0,1]. Reversed-Z is used: near->1, far->0 for precision.
    static Mat4 perspectiveRevZ(float fovY, float aspect, float zn) {
        float f = 1.0f / std::tan(fovY * 0.5f); Mat4 r; std::memset(r.m, 0, sizeof r.m);
        r.m[0] = f / aspect; r.m[5] = f; r.m[11] = -1.0f; r.m[14] = zn; return r;   // infinite far plane
    }
    static Mat4 ortho(float l, float rt, float b, float t, float zn, float zf) {
        Mat4 r; r.m[0] = 2 / (rt - l); r.m[5] = 2 / (t - b); r.m[10] = 1 / (zn - zf);
        r.m[12] = -(rt + l) / (rt - l); r.m[13] = -(t + b) / (t - b); r.m[14] = zn / (zn - zf); return r;
    }
};

struct AABB {
    Vec3 min, max;
    bool intersects(const AABB& o) const { return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y && min.z < o.max.z && max.z > o.min.z; }
    AABB offset(const Vec3& d) const { return {min + d, max + d}; }
};

// Frustum from a view-projection matrix (planes point inward).
struct Frustum {
    Vec4 p[6];
    void build(const Mat4& vp) {
        const float* m = vp.m;
        auto row = [&](int r) { return Vec4{m[r], m[4 + r], m[8 + r], m[12 + r]}; };
        Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        p[0] = {r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w};
        p[1] = {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w};
        p[2] = {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w};
        p[3] = {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w};
        p[4] = {r2.x, r2.y, r2.z, r2.w};                 // reversed-z: z >= 0
        p[5] = {r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w};
    }
    bool visible(const AABB& b) const {
        for (int i = 0; i < 5; i++) {   // skip far plane (infinite projection)
            const Vec4& q = p[i];
            float x = q.x >= 0 ? b.max.x : b.min.x, y = q.y >= 0 ? b.max.y : b.min.y, z = q.z >= 0 ? b.max.z : b.min.z;
            if (q.x * x + q.y * y + q.z * z + q.w < 0) return false;
        }
        return true;
    }
};

constexpr float PI = 3.14159265358979323846f;

} // namespace vx
