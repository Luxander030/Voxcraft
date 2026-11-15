// Shader sources for the two GPU backends. Both versions implement identical maths; keep them in sync.
//
// Terrain vertex = uint3 (see render/mesher.h). Uniform blocks:
//   slot 0 (VS) Frame { float4x4 vp; float4 cam; }          cam.xyz unused, cam.w = time
//   slot 1 (VS) Draw  { float4 origin; }                     section origin relative to the camera
//   slot 2 (FS) Fog   { float4 fogColor; float4 params; }    params: x daylight, y fog start, z fog end, w underwater
// Lines (block outline): float3 position, uniform colour.  UI: float2 pos, float4 (u, v, layer, mode), ubyte4n colour.
#pragma once

namespace vx::shaders {

#if defined(SOKOL_METAL)
// ------------------------------------------------------------------------------------------ Metal
inline const char* terrainVS = R"(
#include <metal_stdlib>
using namespace metal;
struct Frame { float4x4 vp; float4 cam; };
struct Draw { float4 origin; };
struct VIn { uint3 d [[attribute(0)]]; };
struct VOut { float4 pos [[position]]; float3 uvl; float3 tint; float2 light; float shade; float dist; };
constant float faceShade[8] = {0.80, 0.80, 0.55, 1.0, 0.68, 0.68, 0.90, 0.90};
vertex VOut vs_main(VIn in [[stage_in]], constant Frame& f [[buffer(0)]], constant Draw& dr [[buffer(1)]]) {
    VOut o;
    uint w0 = in.d.x, w1 = in.d.y, w2 = in.d.z;
    float3 p = float3(float(w0 & 511u), float((w0 >> 9) & 511u), float((w0 >> 18) & 511u)) * (1.0 / 16.0) + dr.origin.xyz;
    uint face = (w0 >> 27) & 7u; float ao = float(w0 >> 30);
    o.pos = f.vp * float4(p, 1.0);
    o.uvl = float3(float(w1 & 511u) * (1.0 / 16.0), float((w1 >> 9) & 511u) * (1.0 / 16.0), float((w1 >> 18) & 1023u));
    o.tint = float3(float((w2 >> 8) & 255u), float((w2 >> 16) & 255u), float((w2 >> 24) & 255u)) * (1.0 / 255.0);
    o.light = float2(float(w2 & 15u), float((w2 >> 4) & 15u)) * (1.0 / 15.0);
    o.shade = faceShade[face] * (0.55 + 0.15 * ao);
    o.dist = length(p);
    return o;
}
)";
inline const char* terrainFS = R"(
#include <metal_stdlib>
using namespace metal;
struct Fog { float4 fogColor; float4 params; };
struct VOut { float4 pos [[position]]; float3 uvl; float3 tint; float2 light; float shade; float dist; };
static float curve(float l) { return l * l * (3.0 - 2.0 * l) * 0.92 + 0.04; }
static float4 shadeTerrain(VOut in, float4 c, constant Fog& fg) {
    float sky = curve(in.light.x) * fg.params.x, blk = curve(in.light.y);
    float3 lit = float3(max(sky, blk * 1.0), max(sky, blk * 0.92), max(sky, blk * 0.78));
    float3 rgb = c.rgb * in.tint * lit * in.shade;
    float fog = clamp((in.dist - fg.params.y) / max(1.0, fg.params.z - fg.params.y), 0.0, 1.0);
    return float4(mix(rgb, fg.fogColor.rgb, fog * fog), c.a);
}
fragment float4 fs_opaque(VOut in [[stage_in]], constant Fog& fg [[buffer(0)]], texture2d_array<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 c = tex.sample(smp, in.uvl.xy, uint(in.uvl.z));
    return shadeTerrain(in, c, fg);
}
fragment float4 fs_cutout(VOut in [[stage_in]], constant Fog& fg [[buffer(0)]], texture2d_array<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    float4 c = tex.sample(smp, in.uvl.xy, uint(in.uvl.z));
    if (c.a < 0.5) discard_fragment();
    return shadeTerrain(in, float4(c.rgb, 1.0), fg);
}
)";
inline const char* linesVS = R"(
#include <metal_stdlib>
using namespace metal;
struct Frame { float4x4 vp; float4 cam; };
struct Draw { float4 origin; float4 size; };
struct VIn { float3 p [[attribute(0)]]; };
struct VOut { float4 pos [[position]]; };
vertex VOut vs_main(VIn in [[stage_in]], constant Frame& f [[buffer(0)]], constant Draw& dr [[buffer(1)]]) { VOut o; o.pos = f.vp * float4(in.p * dr.size.xyz + dr.origin.xyz, 1.0); o.pos.z += 0.0004 * o.pos.w; return o; }
)";
inline const char* linesFS = R"(
#include <metal_stdlib>
using namespace metal;
struct Col { float4 color; };
struct VOut { float4 pos [[position]]; };
fragment float4 fs_main(VOut in [[stage_in]], constant Col& c [[buffer(0)]]) { return c.color; }
)";
inline const char* uiVS = R"(
#include <metal_stdlib>
using namespace metal;
struct Screen { float4 size; };
struct VIn { float2 p [[attribute(0)]]; float4 t [[attribute(1)]]; float4 c [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float4 t; float4 c; };
vertex VOut vs_main(VIn in [[stage_in]], constant Screen& s [[buffer(0)]]) {
    VOut o; o.pos = float4(in.p.x / s.size.x * 2.0 - 1.0, 1.0 - in.p.y / s.size.y * 2.0, 0.0, 1.0); o.t = in.t; o.c = in.c; return o;
}
)";
inline const char* uiFS = R"(
#include <metal_stdlib>
using namespace metal;
struct VOut { float4 pos [[position]]; float4 t; float4 c; };
fragment float4 fs_main(VOut in [[stage_in]], texture2d<float> font [[texture(0)]], texture2d_array<float> blocks [[texture(1)]], sampler smp [[sampler(0)]]) {
    if (in.t.w < 0.5) return in.c;
    if (in.t.w < 1.5) { float a = font.sample(smp, in.t.xy).a; if (a < 0.5) discard_fragment(); return in.c; }
    float4 c = blocks.sample(smp, in.t.xy, uint(in.t.z));
    if (c.a < 0.5) discard_fragment();
    return float4(c.rgb * in.c.rgb, in.c.a);
}
)";
// Entities: float3 camera-relative position, float4 (u, v, layer, face shade), float2 (sky, block)
// light 0..1, ubyte4n tint. Same lighting/fog maths as terrain.
inline const char* entityVS = R"(
#include <metal_stdlib>
using namespace metal;
struct Frame { float4x4 vp; float4 cam; };
struct VIn { float3 p [[attribute(0)]]; float4 t [[attribute(1)]]; float2 l [[attribute(2)]]; float4 c [[attribute(3)]]; };
struct VOut { float4 pos [[position]]; float3 uvl; float3 tint; float2 light; float shade; float dist; };
vertex VOut vs_main(VIn in [[stage_in]], constant Frame& f [[buffer(0)]]) {
    VOut o; o.pos = f.vp * float4(in.p, 1.0); o.uvl = in.t.xyz; o.tint = in.c.rgb; o.light = in.l; o.shade = in.t.w; o.dist = length(in.p); return o;
}
)";
inline const char* vsEntry = "vs_main";
inline const char* uiFSEntry = "fs_main";
#else
// ------------------------------------------------------------------------------------------ HLSL (D3D11)
inline const char* terrainVS = R"(
cbuffer Frame : register(b0) { float4x4 vp; float4 cam; };
cbuffer Draw : register(b1) { float4 origin; };
struct VIn { uint3 d : TEXCOORD0; };
struct VOut { float3 uvl : TEXCOORD0; float3 tint : TEXCOORD1; float2 light : TEXCOORD2; float shade : TEXCOORD3; float dist : TEXCOORD4; float4 pos : SV_Position; };
static const float faceShade[8] = {0.80, 0.80, 0.55, 1.0, 0.68, 0.68, 0.90, 0.90};
VOut vs_main(VIn i) {
    VOut o;
    uint w0 = i.d.x, w1 = i.d.y, w2 = i.d.z;
    float3 p = float3(float(w0 & 511u), float((w0 >> 9) & 511u), float((w0 >> 18) & 511u)) * (1.0 / 16.0) + origin.xyz;
    uint face = (w0 >> 27) & 7u; float ao = float(w0 >> 30);
    o.pos = mul(vp, float4(p, 1.0));
    o.uvl = float3(float(w1 & 511u) * (1.0 / 16.0), float((w1 >> 9) & 511u) * (1.0 / 16.0), float((w1 >> 18) & 1023u));
    o.tint = float3(float((w2 >> 8) & 255u), float((w2 >> 16) & 255u), float((w2 >> 24) & 255u)) * (1.0 / 255.0);
    o.light = float2(float(w2 & 15u), float((w2 >> 4) & 15u)) * (1.0 / 15.0);
    o.shade = faceShade[face] * (0.55 + 0.15 * ao);
    o.dist = length(p);
    return o;
}
)";
inline const char* terrainFS = R"(
cbuffer Fog : register(b0) { float4 fogColor; float4 params; };
Texture2DArray<float4> tex : register(t0);
SamplerState smp : register(s0);
struct VOut { float3 uvl : TEXCOORD0; float3 tint : TEXCOORD1; float2 light : TEXCOORD2; float shade : TEXCOORD3; float dist : TEXCOORD4; float4 pos : SV_Position; };
float curve(float l) { return l * l * (3.0 - 2.0 * l) * 0.92 + 0.04; }
float4 shadeTerrain(VOut i, float4 c) {
    float sky = curve(i.light.x) * params.x, blk = curve(i.light.y);
    float3 lit = float3(max(sky, blk * 1.0), max(sky, blk * 0.92), max(sky, blk * 0.78));
    float3 rgb = c.rgb * i.tint * lit * i.shade;
    float fog = saturate((i.dist - params.y) / max(1.0, params.z - params.y));
    return float4(lerp(rgb, fogColor.rgb, fog * fog), c.a);
}
float4 fs_opaque(VOut i) : SV_Target0 { return shadeTerrain(i, tex.Sample(smp, i.uvl)); }
float4 fs_cutout(VOut i) : SV_Target0 { float4 c = tex.Sample(smp, i.uvl); if (c.a < 0.5) discard; return shadeTerrain(i, float4(c.rgb, 1.0)); }
)";
inline const char* linesVS = R"(
cbuffer Frame : register(b0) { float4x4 vp; float4 cam; };
cbuffer Draw : register(b1) { float4 origin; float4 size; };
float4 vs_main(float3 p : TEXCOORD0) : SV_Position { float4 o = mul(vp, float4(p * size.xyz + origin.xyz, 1.0)); o.z += 0.0004 * o.w; return o; }
)";
inline const char* linesFS = R"(
cbuffer Col : register(b0) { float4 color; };
float4 fs_main() : SV_Target0 { return color; }
)";
inline const char* uiVS = R"(
cbuffer Screen : register(b0) { float4 size; };
struct VOut { float4 t : TEXCOORD0; float4 c : TEXCOORD1; float4 pos : SV_Position; };
VOut vs_main(float2 p : TEXCOORD0, float4 t : TEXCOORD1, float4 c : TEXCOORD2) {
    VOut o; o.pos = float4(p.x / size.x * 2.0 - 1.0, 1.0 - p.y / size.y * 2.0, 0.0, 1.0); o.t = t; o.c = c; return o;
}
)";
inline const char* uiFS = R"(
Texture2D<float4> font : register(t0);
Texture2DArray<float4> blocks : register(t1);
SamplerState smp : register(s0);
struct VOut { float4 t : TEXCOORD0; float4 c : TEXCOORD1; float4 pos : SV_Position; };
float4 fs_main(VOut i) : SV_Target0 {
    if (i.t.w < 0.5) return i.c;
    if (i.t.w < 1.5) { float a = font.Sample(smp, i.t.xy).a; if (a < 0.5) discard; return i.c; }
    float4 c = blocks.Sample(smp, i.t.xyz);
    if (c.a < 0.5) discard;
    return float4(c.rgb * i.c.rgb, i.c.a);
}
)";
inline const char* entityVS = R"(
cbuffer Frame : register(b0) { float4x4 vp; float4 cam; };
struct VOut { float3 uvl : TEXCOORD0; float3 tint : TEXCOORD1; float2 light : TEXCOORD2; float shade : TEXCOORD3; float dist : TEXCOORD4; float4 pos : SV_Position; };
VOut vs_main(float3 p : TEXCOORD0, float4 t : TEXCOORD1, float2 l : TEXCOORD2, float4 c : TEXCOORD3) {
    VOut o; o.pos = mul(vp, float4(p, 1.0)); o.uvl = t.xyz; o.tint = c.rgb; o.light = l; o.shade = t.w; o.dist = length(p); return o;
}
)";
inline const char* vsEntry = "vs_main";
inline const char* uiFSEntry = "fs_main";
#endif

} // namespace vx::shaders
