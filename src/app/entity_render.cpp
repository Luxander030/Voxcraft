// Builds the per-frame entity mesh: creatures (CPU-skinned box models) and dropped items.
// Culled by distance and frustum; distant creatures animate at a quarter rate (LOD).
#include "app/game.h"

namespace vx {

namespace {
const float FACE_SHADE[6] = {0.80f, 0.80f, 0.55f, 1.0f, 0.68f, 0.68f};
const int UAX[3] = {2, 0, 0}, VAX[3] = {1, 2, 1};

Vec3 xform(const Mat4& m, Vec3 p) { Vec4 r = m * Vec4{p.x, p.y, p.z, 1}; return {r.x, r.y, r.z}; }

// Emits a box (pixel units, model space) transformed by `m` into camera-relative world space.
void emitBox(std::vector<EntityVertex>& out, const Mat4& m, Vec3 mn, Vec3 size, const u16 tex[6], float sky, float blk, u32 tint) {
    Vec3 mx = mn + size;
    for (int f = 0; f < 6; f++) {
        int a = f >> 1, ua = UAX[a], va = VAX[a]; bool pos = f & 1;
        Vec3 c[4]; float uv[4][2];
        const int cu[4] = {0, 1, 1, 0}, cv[4] = {0, 0, 1, 1};
        float uSpan = size[ua] / 16.0f, vSpan = size[va] / 16.0f;
        for (int k = 0; k < 4; k++) {
            Vec3 p; p[a] = pos ? mx[a] : mn[a]; p[ua] = cu[k] ? mx[ua] : mn[ua]; p[va] = cv[k] ? mx[va] : mn[va];
            c[k] = xform(m, p);
            float u = cu[k] * uSpan, v = (a == 1) ? cv[k] * vSpan : (1 - cv[k]) * vSpan;
            if (f == FACE_PX || f == FACE_NZ) u = uSpan - u;
            uv[k][0] = u; uv[k][1] = v;
        }
        int order[4] = {0, 1, 2, 3};
        if (f == FACE_PX || f == FACE_PY || f == FACE_NZ) { order[1] = 3; order[3] = 1; }
        for (int k : order) out.push_back({c[k].x, c[k].y, c[k].z, uv[k][0], uv[k][1], (float)tex[f], FACE_SHADE[f], sky, blk, tint});
    }
}

void lightAt(Vec3 p, float& sky, float& blk) {
    BlockPos b{(int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z)};
    sky = G->world->skyLight(b) / 15.0f; blk = G->world->blockLight(b) / 15.0f;
}
} // namespace

void buildEntityMesh(std::vector<EntityVertex>& out, Vec3 eye, float alpha, float dt) {
    out.clear();
    const Frustum& fr = G->renderer.lastFrustum;
    float maxD = (float)std::min(G->opt.radius * 16, 96);
    std::vector<Mat4> bones;
    // creatures
    for (auto& m : G->mobs->mobs) {
        const MobDef& d = G->mobReg.def(m.type);
        Vec3 pos = m.prevPos + (m.pos - m.prevPos) * alpha;
        Vec3 rel = pos - eye;
        float dist = length(rel);
        if (dist > maxD) continue;
        if (!fr.visible({rel - Vec3(d.width, 0, d.width), rel + Vec3(d.width, d.height, d.width)})) continue;
        // animation LOD: far creatures update 4x less often (accumulating time)
        float hspeed = std::sqrt((m.pos.x - m.prevPos.x) * (m.pos.x - m.prevPos.x) + (m.pos.z - m.prevPos.z) * (m.pos.z - m.prevPos.z));
        if (dist < 32 || (G->frame + (u64)(uintptr_t)&m / 64) % 4 == 0) m.anim.update(dist < 32 ? dt : dt * 4, {hspeed, m.onGround, m.headYaw, m.headPitch});
        m.anim.boneMatrices(bones);
        float yaw = m.prevYaw + (float)std::remainder(m.yaw - m.prevYaw, 2 * PI) * alpha;
        Mat4 world = Mat4::translate(rel.x, rel.y, rel.z) * Mat4::rotateY(-yaw) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
        float sky, blk; lightAt(pos + Vec3(0, d.height * 0.5f, 0), sky, blk);
        u32 tint = m.hurtTicks > 0 || !m.alive() ? rgba(255, 110, 110) : rgba(255, 255, 255);
        const Model& model = G->mobReg.model(d.model);
        for (auto& b : model.boxes) emitBox(out, world * bones[b.bone], b.min, b.size, b.tex, sky, blk, tint);
    }
    // dropped items: small spinning cubes (block items) or crossed sprites
    for (auto& e : G->entities->items) {
        Vec3 rel = e.pos - eye;
        if (dot(rel, rel) > 48 * 48) continue;
        const ItemDef& def = G->items.def(e.stack.item);
        float bob = std::sin((e.age + alpha) * 0.1f) * 0.06f + 0.1f;
        float spin = (e.age + alpha) * 0.05f;
        float sky, blk; lightAt(e.pos + Vec3(0, 0.2f, 0), sky, blk);
        Mat4 world = Mat4::translate(rel.x, rel.y + bob, rel.z) * Mat4::rotateY(spin) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
        if (def.texture < 0 && def.places != AIR && G->reg.def(def.places).shape == BlockShape::Cube) {
            u16 t[6]; for (int f = 0; f < 6; f++) t[f] = G->renderer.textures().face(def.places, f);
            u32 tint = G->reg.def(def.places).tint == Tint::None ? rgba(255, 255, 255) : rgba(120, 180, 80);
            emitBox(out, world, {-2, 0, -2}, {4, 4, 4}, t, sky, blk, tint);
            if (e.stack.count > 1) emitBox(out, world * Mat4::translate(1.5f, 1.5f, 1.0f), {-2, 0, -2}, {4, 4, 4}, t, sky, blk, tint);
        } else {
            int layer = def.texture >= 0 ? def.texture : G->renderer.textures().face(def.places, FACE_NZ);
            u16 t[6] = {(u16)layer, (u16)layer, (u16)layer, (u16)layer, (u16)layer, (u16)layer};
            emitBox(out, world, {-4, 0, -0.05f}, {8, 8, 0.1f}, t, sky, blk, rgba(255, 255, 255));   // thin double-sided card
        }
    }
}

} // namespace vx
