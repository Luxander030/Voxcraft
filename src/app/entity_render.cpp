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

// An item stack as a spinning-pickup style model: a small cube for full blocks, otherwise a thin
// double-sided card. `world` maps pixel units; the model sits on y = 0, centred on x/z.
void emitItemModel(std::vector<EntityVertex>& out, const Mat4& world, const ItemStack& st, float sky, float blk, bool pile) {
    const ItemDef& def = G->items.def(st.item);
    if (def.texture < 0 && def.places != AIR && G->reg.def(def.places).shape == BlockShape::Cube) {
        u16 t[6]; for (int f = 0; f < 6; f++) t[f] = G->renderer.textures().face(def.places, f);
        u32 tint = G->reg.def(def.places).tint == Tint::None ? rgba(255, 255, 255) : rgba(120, 180, 80);
        emitBox(out, world, {-2, 0, -2}, {4, 4, 4}, t, sky, blk, tint);
        if (pile && st.count > 1) emitBox(out, world * Mat4::translate(1.5f, 1.5f, 1.0f), {-2, 0, -2}, {4, 4, 4}, t, sky, blk, tint);
    } else {
        int layer = def.texture >= 0 ? def.texture : G->renderer.textures().face(def.places, FACE_NZ);
        u16 t[6] = {(u16)layer, (u16)layer, (u16)layer, (u16)layer, (u16)layer, (u16)layer};
        emitBox(out, world, {-4, 0, -0.05f}, {8, 8, 0.1f}, t, sky, blk, rgba(255, 255, 255));
    }
}

void lightAt(Vec3 p, float& sky, float& blk) {
    BlockPos b{(int)std::floor(p.x), (int)std::floor(p.y), (int)std::floor(p.z)};
    sky = G->world->skyLight(b) / 15.0f; blk = G->world->blockLight(b) / 15.0f;
}
} // namespace

// Sky objects use shade 2.0: the shader skips lighting and fog for them. They are far away, so
// terrain in front occludes them through the normal depth test.
static void skyQuad(std::vector<EntityVertex>& out, Vec3 center, Vec3 right, Vec3 up, int layer, u32 tint) {
    Vec3 q[4] = {center - right - up, center + right - up, center + right + up, center - right + up};
    const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int k = 0; k < 4; k++) out.push_back({q[k].x, q[k].y, q[k].z, uv[k][0], uv[k][1], (float)layer, 2.0f, 1, 1, tint});
    for (int k = 3; k >= 0; k--) out.push_back({q[k].x, q[k].y, q[k].z, uv[k][0], uv[k][1], (float)layer, 2.0f, 1, 1, tint});   // double-sided
}

static void buildSky(std::vector<EntityVertex>& out, Vec3 eye) {
    if (G->dimension != "overworld") return;
    float ang = (float)(G->world->meta.dayTime - 6000) / 24000.0f * 2 * PI;
    Vec3 sunDir{-std::sin(ang), std::cos(ang), 0.15f};
    sunDir = normalize(sunDir);
    float day = clamp(0.5f + std::cos(ang) * 0.9f, 0.0f, 1.0f), night = 1 - day;
    if (G->world->meta.raining) day *= 0.6f;
    auto billboard = [&](Vec3 dir, float dist, float size, int layer, u32 tint) {
        Vec3 r = normalize(cross(dir, {0, 0, 1})); if (length(r) < 0.1f) r = {1, 0, 0};
        Vec3 u = normalize(cross(r, dir));
        Vec3 c = dir * dist;
        // facing the camera: winding must be CCW as seen from the origin
        skyQuad(out, c, r * -size, u * size, layer, tint);
    };
    if (!G->world->meta.raining) {
        billboard(sunDir, 300, 30, G->pack.layer("sun"), rgba(255, 255, 255));
        billboard(sunDir * -1.0f, 300, 22, G->pack.layer("moon"), rgba(255, 255, 255));
        if (night > 0.3f) {   // stars: fixed pattern, brighter as the night deepens
            Rng sr(0x57A75);
            u8 b = (u8)(255 * clamp((night - 0.3f) * 1.6f, 0.0f, 1.0f));
            int snow = std::max(0, G->pack.layer("snow"));
            for (int i = 0; i < 260; i++) {
                Vec3 d = normalize(Vec3(sr.unit() * 2 - 1, sr.unit() * 0.9f + 0.1f, sr.unit() * 2 - 1));
                billboard(d, 340, 0.6f + sr.unit() * 0.5f, snow, rgba(b, b, b));
            }
        }
    }
    // clouds: 12-block cells from a fixed noise field drifting east
    static Noise cn(0xC10D);
    int cl = G->pack.layer("cloud");
    float drift = (float)G->world->meta.tick * 0.03f;
    const float cell = 12, cy = 180;
    int cx0 = (int)std::floor((eye.x - drift) / cell), cz0 = (int)std::floor(eye.z / cell);
    u8 cb = (u8)(25 + 225 * day); u32 tint = G->world->meta.raining ? rgba((u8)(cb * 0.7f), (u8)(cb * 0.7f), (u8)(cb * 0.75f)) : rgba(cb, cb, cb);
    u16 t[6] = {(u16)cl, (u16)cl, (u16)cl, (u16)cl, (u16)cl, (u16)cl};
    for (int dz = -14; dz <= 14; dz++) for (int dx = -14; dx <= 14; dx++) {
        if (dx * dx + dz * dz > 196) continue;
        int gx = cx0 + dx, gz = cz0 + dz;
        float n = cn.noise2(gx * 0.18f, gz * 0.18f) + (G->world->meta.raining ? 0.35f : 0);
        if (n < 0.25f) continue;
        Vec3 o{gx * cell + drift - eye.x, cy - eye.y, gz * cell - eye.z};
        size_t before = out.size();
        emitBox(out, Mat4::translate(o.x, o.y, o.z), {0, 0, 0}, {cell, 4, cell}, t, 1, 1, tint);
        for (size_t k = before; k < out.size(); k++) out[k].shade = 2.0f;
    }
}

void buildEntityMesh(std::vector<EntityVertex>& out, Vec3 eye, float alpha, float dt) {
    out.clear();
    buildSky(out, eye);
    // crack overlay on the block being broken: a decal box 0.05 px outside its faces
    if (G->breakProgress > 0 && !G->creative() && G->target.hit) {
        int stage = clamp((int)(G->breakProgress * 8), 0, 7);
        int layer = G->pack.layer("crack_" + std::to_string(stage));
        if (layer >= 0) {
            const BlockPos& b = G->target.block;
            AABB sb = blockShapeBounds(G->reg, G->world->getBlock(b));
            Mat4 m = Mat4::translate(b.x - eye.x, b.y - eye.y, b.z - eye.z) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
            u16 t[6]; for (auto& x : t) x = (u16)layer;
            Vec3 mn = sb.min * 16.0f - Vec3(0.05f, 0.05f, 0.05f), sz = (sb.max - sb.min) * 16.0f + Vec3(0.1f, 0.1f, 0.1f);
            float sky, blk; lightAt({b.x + 0.5f, b.y + 1.2f, b.z + 0.5f}, sky, blk);
            emitBox(out, m, mn, sz, t, std::max(sky, 0.6f), blk, rgba(255, 255, 255));
        }
    }
    // particles: camera-facing quads
    {
        float yaw = G->camYaw, pitch = G->camPitch;
        Vec3 f{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
        Vec3 r = normalize(cross(f, {0, 1, 0})), u = cross(r, f);
        for (const Particle& p : G->particles.parts) {
            Vec3 c = p.pos - eye;
            if (dot(c, c) > 64 * 64 || dot(c, f) < 0) continue;
            float sky, blk;
            if (p.emissive) { sky = 1; blk = 1; } else lightAt(p.pos, sky, blk);
            float h = p.size * 0.5f, hv = h * p.stretch;
            Vec3 vu = p.stretch > 1 ? Vec3(0, 1, 0) : u;   // stretched particles (rain) stay vertical
            Vec3 q[4] = {c - r * h - vu * hv, c + r * h - vu * hv, c + r * h + vu * hv, c - r * h + vu * hv};
            float u0 = p.u0, v0 = p.v0, us = p.uvSize;
            const float uv[4][2] = {{u0, v0 + us}, {u0 + us, v0 + us}, {u0 + us, v0}, {u0, v0}};
            for (int k = 0; k < 4; k++) out.push_back({q[k].x, q[k].y, q[k].z, uv[k][0], uv[k][1], (float)p.layer, 1.0f, sky, blk, p.tint});
        }
    }
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
        float sc = m.scale() / 16.0f;
        Mat4 world = Mat4::translate(rel.x, rel.y, rel.z) * Mat4::rotateY(-yaw) * Mat4::scale(sc, sc, sc);
        float sky, blk; lightAt(pos + Vec3(0, d.height * 0.5f * m.scale(), 0), sky, blk);
        if (d.emissive) { sky = 1; blk = 1; }
        u32 tint = m.hurtTicks > 0 || !m.alive() ? rgba(255, 110, 110) : rgba(255, 255, 255);
        const Model& model = G->mobReg.model(d.model);
        int head = m.baby() ? model.bone("head") : -1;
        Mat4 headScale;   // babies: a proportionally larger head, scaled about its pivot
        if (head >= 0) { Vec3 pv = model.bones[head].pivot; headScale = Mat4::translate(pv.x, pv.y, pv.z) * Mat4::scale(1.5f, 1.5f, 1.5f) * Mat4::translate(-pv.x, -pv.y, -pv.z); }
        for (auto& b : model.boxes) emitBox(out, b.bone == head ? world * bones[b.bone] * headScale : world * bones[b.bone], b.min, b.size, b.tex, sky, blk, tint);
    }
    // carts
    if (int cm = G->mobReg.modelIndex("cart"); cm >= 0) {
        const Model& model = G->mobReg.model(cm);
        for (const Cart& c : G->carts->carts) {
            Vec3 pos = c.prevPos + (c.pos - c.prevPos) * alpha, rel = pos - eye;
            if (dot(rel, rel) > maxD * maxD) continue;
            if (!fr.visible({rel - Vec3(0.6f, 0, 0.6f), rel + Vec3(0.6f, 0.8f, 0.6f)})) continue;
            float yaw = c.prevYaw + (float)std::remainder(c.yaw - c.prevYaw, 2 * PI) * alpha;
            float pitch = c.prevPitch + (c.pitch - c.prevPitch) * alpha;
            Mat4 world = Mat4::translate(rel.x, rel.y + 1 / 16.0f, rel.z) * Mat4::rotateY(-yaw) * Mat4::rotateX(pitch) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
            float sky, blk; lightAt(pos + Vec3(0, 0.5f, 0), sky, blk);
            u32 tint = c.hurtTicks > 0 ? rgba(255, 110, 110) : rgba(255, 255, 255);
            for (auto& b : model.boxes) emitBox(out, world, b.min, b.size, b.tex, sky, blk, tint);
            if (c.kind == CartKind::Storage) {   // a 12 px chest sitting in the tub, using the chest block's faces
                static const BlockState chest = G->reg.stateOf("chest");
                u16 t[6]; for (int f = 0; f < 6; f++) t[f] = G->renderer.textures().face(chest, f);
                emitBox(out, world, {-6, 3, -6}, {12, 12, 12}, t, sky, blk, tint);
            }
        }
    }
    // projectiles: arrows as thin shafts along their velocity, bolts as glowing cubes
    for (const Projectile& pr : G->projectiles.list) {
        Vec3 pos = pr.prevPos + (pr.pos - pr.prevPos) * alpha, rel = pos - eye;
        if (dot(rel, rel) > 64 * 64) continue;
        if (pr.kind == ProjKind::EmberBolt) {
            int l = std::max(0, G->pack.layer("cinderwisp_core"));
            u16 t[6] = {(u16)l, (u16)l, (u16)l, (u16)l, (u16)l, (u16)l};
            Mat4 m = Mat4::translate(rel.x, rel.y, rel.z) * Mat4::rotateY((G->now) * 6.0f) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
            emitBox(out, m, {-2, -2, -2}, {4, 4, 4}, t, 1, 1, rgba(255, 255, 255));
            continue;
        }
        Vec3 v = pr.vel; float hz = std::sqrt(v.x * v.x + v.z * v.z);
        float yaw = std::atan2(v.x, -v.z), pitch = std::atan2(v.y, std::max(hz, 1e-5f));
        Mat4 m = Mat4::translate(rel.x, rel.y, rel.z) * Mat4::rotateY(-yaw) * Mat4::rotateX(pitch) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
        int shaft = std::max(0, G->pack.layer("oak_log")), tip = std::max(0, G->pack.layer("iron_block"));
        u16 ts[6] = {(u16)shaft, (u16)shaft, (u16)shaft, (u16)shaft, (u16)shaft, (u16)shaft}, tt[6] = {(u16)tip, (u16)tip, (u16)tip, (u16)tip, (u16)tip, (u16)tip};
        float sky, blk; lightAt(pos, sky, blk);
        emitBox(out, m, {-0.5f, -0.5f, -5}, {1, 1, 9}, ts, sky, blk, rgba(255, 255, 255));
        emitBox(out, m, {-1, -1, -7}, {2, 2, 2}, tt, sky, blk, rgba(255, 255, 255));
    }
    // dropped items: small spinning cubes (block items) or crossed sprites
    for (auto& e : G->entities->items) {
        Vec3 rel = e.pos - eye;
        if (dot(rel, rel) > 48 * 48) continue;
        float bob = std::sin((e.age + alpha) * 0.1f) * 0.06f + 0.1f;
        float spin = (e.age + alpha) * 0.05f;
        float sky, blk; lightAt(e.pos + Vec3(0, 0.2f, 0), sky, blk);
        Mat4 world = Mat4::translate(rel.x, rel.y + bob, rel.z) * Mat4::rotateY(spin) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
        emitItemModel(out, world, e.stack, sky, blk, true);
    }
}

void buildPlayerMesh(std::vector<EntityVertex>& out, Vec3 eye, Vec3 playerPos, float alpha, float dt) {
    (void)alpha;
    const Player& p = G->player;
    Animator& a = G->playerAnim;
    if (G->stats.dead) a.play(AnimState::Death);
    else if (G->swingRequested || (G->mouseDown[0] && G->mouseLocked && G->screen == Screen::None && G->target.hit && a.state() != AnimState::Attack)) a.play(AnimState::Attack);
    G->swingRequested = false;
    AnimInput in;
    in.speed = std::sqrt((p.pos.x - p.prevPos.x) * (p.pos.x - p.prevPos.x) + (p.pos.z - p.prevPos.z) * (p.pos.z - p.prevPos.z));
    in.onGround = p.onGround || p.flying;
    in.headPitch = p.pitch;
    in.sneaking = p.sneaking && !p.flying;
    in.swimming = p.headInFluid && !p.onGround;
    in.climbing = p.onClimbable;
    in.holdArm = (G->eatTicks > 0 || G->bowCharge > 0) ? 1.0f : 0.0f;
    if (in.swimming) in.headPitch = 0;
    a.update(dt, in);
    std::vector<Mat4>& bones = G->scratchBones;
    a.boneMatrices(bones);
    const Model& model = G->mobReg.model(G->playerModel);
    Vec3 rel = playerPos - eye;
    Mat4 world = Mat4::translate(rel.x, rel.y, rel.z) * Mat4::rotateY(-p.yaw) * Mat4::scale(1 / 16.0f, 1 / 16.0f, 1 / 16.0f);
    float sky, blk; lightAt(playerPos + Vec3(0, 1.0f, 0), sky, blk);
    u32 tint = a.state() == AnimState::Hurt || G->stats.dead ? rgba(255, 110, 110) : rgba(255, 255, 255);
    for (auto& b : model.boxes) emitBox(out, world * bones[b.bone], b.min, b.size, b.tex, sky, blk, tint);
    // held item, in the right hand: rotated to point forward, seen side-on
    const ItemStack& held = G->inv.slots[G->hotbarSel];
    int arm = model.bone("arm_r");
    if (!held.empty() && arm >= 0) {
        Mat4 hand = world * bones[arm] * Mat4::translate(6, 10, -1) * Mat4::rotateX(-PI * 0.5f) * Mat4::rotateY(PI * 0.5f) * Mat4::translate(0, -1, 0);
        emitItemModel(out, hand, held, sky, blk, false);
    }
}

} // namespace vx
