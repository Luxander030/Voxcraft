#include "app/renderer.h"
#include "app/shaders.h"
#include "core/profiler.h"
#include "render/visibility.h"

namespace vx {

namespace {
constexpr int MAX_QUADS = 16384;   // per section: 16^3 checkerboard worst case is 12288 visible faces
constexpr int VERTEX_BYTES = 12;

Mat4 viewMatrix(float yaw, float pitch) {
    Vec3 f{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    Vec3 r = normalize(cross(f, {0, 1, 0})), u = cross(r, f);
    Mat4 m;
    m.m[0] = r.x; m.m[4] = r.y; m.m[8] = r.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;
    m.m[12] = m.m[13] = m.m[14] = 0;
    return m;
}

sg_shader makeShader(const char* vs, const char* fs, const char* fsEntry, int kind, const char* label) {
    sg_shader_desc d = {};
    d.vertex_func.source = vs; d.vertex_func.entry = shaders::vsEntry;
    d.fragment_func.source = fs; d.fragment_func.entry = fsEntry;
    d.label = label;
    auto ub = [&](int slot, sg_shader_stage st, int size, int reg) {
        d.uniform_blocks[slot].stage = st; d.uniform_blocks[slot].size = (u32)size;
        d.uniform_blocks[slot].hlsl_register_b_n = (u8)reg; d.uniform_blocks[slot].msl_buffer_n = (u8)reg;
    };
    if (kind == 0) {   // terrain
        d.attrs[0].base_type = SG_SHADERATTRBASETYPE_UINT; d.attrs[0].hlsl_sem_name = "TEXCOORD"; d.attrs[0].hlsl_sem_index = 0;
        ub(0, SG_SHADERSTAGE_VERTEX, 80, 0); ub(1, SG_SHADERSTAGE_VERTEX, 16, 1); ub(2, SG_SHADERSTAGE_FRAGMENT, 32, 0);
        d.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT; d.views[0].texture.image_type = SG_IMAGETYPE_ARRAY;
        d.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        d.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT; d.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        d.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT; d.texture_sampler_pairs[0].view_slot = 0; d.texture_sampler_pairs[0].sampler_slot = 0;
    } else if (kind == 3) {   // entities
        for (int i = 0; i < 4; i++) { d.attrs[i].base_type = SG_SHADERATTRBASETYPE_FLOAT; d.attrs[i].hlsl_sem_name = "TEXCOORD"; d.attrs[i].hlsl_sem_index = (u8)i; }
        ub(0, SG_SHADERSTAGE_VERTEX, 80, 0); ub(2, SG_SHADERSTAGE_FRAGMENT, 32, 0);
        d.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT; d.views[0].texture.image_type = SG_IMAGETYPE_ARRAY;
        d.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        d.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT; d.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        d.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT; d.texture_sampler_pairs[0].view_slot = 0; d.texture_sampler_pairs[0].sampler_slot = 0;
    } else if (kind == 1) {   // lines
        d.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT; d.attrs[0].hlsl_sem_name = "TEXCOORD";
        ub(0, SG_SHADERSTAGE_VERTEX, 80, 0); ub(1, SG_SHADERSTAGE_VERTEX, 32, 1); ub(2, SG_SHADERSTAGE_FRAGMENT, 16, 0);
    } else {   // ui
        for (int i = 0; i < 3; i++) { d.attrs[i].base_type = SG_SHADERATTRBASETYPE_FLOAT; d.attrs[i].hlsl_sem_name = "TEXCOORD"; d.attrs[i].hlsl_sem_index = (u8)i; }
        ub(0, SG_SHADERSTAGE_VERTEX, 16, 0);
        d.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT; d.views[0].texture.image_type = SG_IMAGETYPE_2D; d.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        d.views[0].texture.hlsl_register_t_n = 0; d.views[0].texture.msl_texture_n = 0;
        d.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT; d.views[1].texture.image_type = SG_IMAGETYPE_ARRAY; d.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        d.views[1].texture.hlsl_register_t_n = 1; d.views[1].texture.msl_texture_n = 1;
        d.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT; d.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        for (int i = 0; i < 2; i++) { d.texture_sampler_pairs[i].stage = SG_SHADERSTAGE_FRAGMENT; d.texture_sampler_pairs[i].view_slot = (u8)i; d.texture_sampler_pairs[i].sampler_slot = 0; }
    }
    return sg_make_shader(&d);
}
} // namespace

bool Renderer::init(const BlockRegistry& reg, const TexturePack& pack, JobSystem& jobs, std::string* err) {
    reg_ = &reg; jobs_ = &jobs;
    if (!tex_.build(reg, pack, err)) return false;
    mctx_.reg = reg_; mctx_.tex = &tex_;

    // block texture array with full mip chain
    int levels = 0; std::vector<u8> mips = pack.buildMips(levels);
    sg_image_desc id = {};
    id.type = SG_IMAGETYPE_ARRAY; id.width = id.height = pack.tileSize; id.num_slices = pack.tileCount; id.num_mipmaps = levels;
    id.pixel_format = SG_PIXELFORMAT_RGBA8; id.data = {mips.data(), mips.size()}; id.label = "blocks";
    blockImg_ = sg_make_image(&id);
    sg_view_desc vd = {}; vd.texture.image = blockImg_; blockView_ = sg_make_view(&vd);
    sg_sampler_desc sd = {}; sd.min_filter = SG_FILTER_NEAREST; sd.mag_filter = SG_FILTER_NEAREST; sd.mipmap_filter = SG_FILTER_NEAREST;
    sd.wrap_u = sd.wrap_v = SG_WRAP_REPEAT; sd.label = "blocks";
    blockSmp_ = sg_make_sampler(&sd);

    int fw, fh; std::vector<u8> font = buildFontAtlas(fw, fh);
    sg_image_desc fd = {}; fd.width = fw; fd.height = fh; fd.pixel_format = SG_PIXELFORMAT_RGBA8; fd.data = {font.data(), font.size()}; fd.label = "font";
    fontImg_ = sg_make_image(&fd);
    sg_view_desc fvd = {}; fvd.texture.image = fontImg_; fontView_ = sg_make_view(&fvd);
    sg_sampler_desc usd = {}; usd.min_filter = usd.mag_filter = SG_FILTER_NEAREST; usd.wrap_u = usd.wrap_v = SG_WRAP_CLAMP_TO_EDGE; usd.label = "ui";
    uiSmp_ = sg_make_sampler(&usd);

    // shared quad index buffer
    std::vector<u16> idx; idx.reserve(MAX_QUADS * 6);
    for (int q = 0; q < MAX_QUADS; q++) { u16 b = (u16)(q * 4); u16 t[6] = {b, (u16)(b + 1), (u16)(b + 2), b, (u16)(b + 2), (u16)(b + 3)}; idx.insert(idx.end(), t, t + 6); }
    sg_buffer_desc ib = {}; ib.usage.index_buffer = true; ib.data = {idx.data(), idx.size() * 2}; ib.label = "quad-indices";
    quadIdx_ = sg_make_buffer(&ib);

    // unit cube outline (expanded slightly so it is not hidden by the block faces)
    const float e = 0.002f, lo = -e, hi = 1 + e;
    float c[8][3]; for (int i = 0; i < 8; i++) { c[i][0] = (i & 1) ? hi : lo; c[i][1] = (i & 2) ? hi : lo; c[i][2] = (i & 4) ? hi : lo; }
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::vector<float> lines; for (auto& ed : edges) for (int k : ed) lines.insert(lines.end(), c[k], c[k] + 3);
    sg_buffer_desc ob = {}; ob.usage.vertex_buffer = true; ob.data = {lines.data(), lines.size() * 4}; ob.label = "outline";
    outlineBuf_ = sg_make_buffer(&ob);

    uiCapacity_ = 1u << 16;
    sg_buffer_desc ub = {}; ub.usage.vertex_buffer = true; ub.usage.write_transient = true; ub.size = uiCapacity_ * sizeof(UiVertex); ub.label = "ui";
    uiBuf_ = sg_make_buffer(&ub);

    entityCapacity_ = MAX_QUADS * 4;
    sg_buffer_desc eb = {}; eb.usage.vertex_buffer = true; eb.usage.write_transient = true; eb.size = entityCapacity_ * sizeof(EntityVertex); eb.label = "entities";
    entityBuf_ = sg_make_buffer(&eb);
    entitySh_ = makeShader(shaders::entityVS, shaders::terrainFS, "fs_cutout", 3, "entity");
    terrainOpaqueSh_ = makeShader(shaders::terrainVS, shaders::terrainFS, "fs_opaque", 0, "terrain-opaque");
    terrainCutoutSh_ = makeShader(shaders::terrainVS, shaders::terrainFS, "fs_cutout", 0, "terrain-cutout");
    linesSh_ = makeShader(shaders::linesVS, shaders::linesFS, "fs_main", 1, "lines");
    uiSh_ = makeShader(shaders::uiVS, shaders::uiFS, shaders::uiFSEntry, 2, "ui");

    auto terrainPip = [&](sg_shader sh, bool blend, bool depthWrite, sg_cull_mode cull, const char* label) {
        sg_pipeline_desc p = {};
        p.shader = sh; p.layout.buffers[0].stride = VERTEX_BYTES; p.layout.attrs[0].format = SG_VERTEXFORMAT_UINT3;
        p.index_type = SG_INDEXTYPE_UINT16; p.cull_mode = cull; p.face_winding = SG_FACEWINDING_CCW;
        p.depth.compare = SG_COMPAREFUNC_GREATER_EQUAL; p.depth.write_enabled = depthWrite;   // reversed Z
        if (blend) {
            p.colors[0].blend.enabled = true;
            p.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA; p.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            p.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE; p.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        }
        p.label = label;
        return sg_make_pipeline(&p);
    };
    opaquePip_ = terrainPip(terrainOpaqueSh_, false, true, SG_CULLMODE_BACK, "opaque");
    cutoutPip_ = terrainPip(terrainCutoutSh_, false, true, SG_CULLMODE_BACK, "cutout");
    translucentPip_ = terrainPip(terrainOpaqueSh_, true, false, SG_CULLMODE_BACK, "translucent");

    sg_pipeline_desc ep = {};
    ep.shader = entitySh_;
    ep.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; ep.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
    ep.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2; ep.layout.attrs[3].format = SG_VERTEXFORMAT_UBYTE4N;
    ep.index_type = SG_INDEXTYPE_UINT16; ep.cull_mode = SG_CULLMODE_BACK; ep.face_winding = SG_FACEWINDING_CCW;
    ep.depth.compare = SG_COMPAREFUNC_GREATER_EQUAL; ep.depth.write_enabled = true; ep.label = "entities";
    entityPip_ = sg_make_pipeline(&ep);

    sg_pipeline_desc lp = {};
    lp.shader = linesSh_; lp.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3; lp.primitive_type = SG_PRIMITIVETYPE_LINES;
    lp.depth.compare = SG_COMPAREFUNC_GREATER_EQUAL; lp.depth.write_enabled = false;
    lp.colors[0].blend.enabled = true; lp.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA; lp.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    lp.label = "lines";
    linesPip_ = sg_make_pipeline(&lp);

    sg_pipeline_desc up = {};
    up.shader = uiSh_;
    up.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2; up.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4; up.layout.attrs[2].format = SG_VERTEXFORMAT_UBYTE4N;
    up.depth.compare = SG_COMPAREFUNC_ALWAYS; up.depth.write_enabled = false;
    up.colors[0].blend.enabled = true; up.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA; up.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    up.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE; up.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    up.label = "ui";
    uiPip_ = sg_make_pipeline(&up);

    for (sg_pipeline pp : {opaquePip_, cutoutPip_, translucentPip_, linesPip_, uiPip_, entityPip_})
        if (sg_query_pipeline_state(pp) != SG_RESOURCESTATE_VALID) { if (err) *err = "pipeline/shader creation failed (see log)"; return false; }
    return true;
}

void Renderer::detach() {
    // New epoch: mesh jobs still in flight for the old world are dropped when they complete.
    alive_->store(false);
    alive_ = std::make_shared<std::atomic<bool>>(true);
    for (auto& [p, c] : chunks_) for (auto& s : c.sec) freeSection(s);
    chunks_.clear();
    if (world_) world_->setListener(nullptr);
    world_ = nullptr;
}

void Renderer::shutdown() {
    alive_->store(false);
    detach();
}

void Renderer::freeSection(SectionGpu& s) {
    if (s.buf.id) { sg_destroy_buffer(s.buf); s.buf = {}; }
    s.bytes = 0; for (int l = 0; l < ML_COUNT; l++) s.count[l] = s.first[l] = 0;
}

void Renderer::markDirty(ChunkPos p, u16 mask) {
    auto it = chunks_.find(p);
    if (it == chunks_.end()) { if (!world_ || !world_->isReady(p)) return; it = chunks_.emplace(p, ChunkGpu{}).first; }
    it->second.dirty |= mask;
}

void Renderer::onChunkReady(ChunkPos p) {
    chunks_.emplace(p, ChunkGpu{});
    markDirty(p, 0xFFFF);
    for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) if (dx || dz) {
        auto it = chunks_.find({p.x + dx, p.z + dz});
        if (it != chunks_.end()) it->second.dirty = 0xFFFF;   // border faces/light of neighbours depend on this chunk
    }
}

void Renderer::onSectionsChanged(ChunkPos p, u16 mask) {
    markDirty(p, mask);
    // smooth lighting at chunk borders samples the neighbour's light
    for (ChunkPos n : {ChunkPos{p.x - 1, p.z}, ChunkPos{p.x + 1, p.z}, ChunkPos{p.x, p.z - 1}, ChunkPos{p.x, p.z + 1}}) {
        auto it = chunks_.find(n); if (it != chunks_.end()) it->second.dirty |= mask;
    }
}

void Renderer::onChunkUnloaded(ChunkPos p) {
    auto it = chunks_.find(p);
    if (it == chunks_.end()) return;
    for (auto& s : it->second.sec) freeSection(s);
    chunks_.erase(it);
}

void Renderer::upload(ChunkPos p, u32 gen, int sy, const SectionMesh& m) {
    auto it = chunks_.find(p);
    if (it == chunks_.end() || it->second.gen != gen) return;   // unloaded or superseded
    SectionGpu& s = it->second.sec[sy];
    freeSection(s);
    s.conn = m.connectivity;   // kept for empty sections too: they still block or pass visibility
    if (m.empty()) return;
    Stopwatch sw;
    std::vector<u32> all; all.reserve(m.bytes() / 4);
    int first = 0;
    for (int l = 0; l < ML_COUNT; l++) {
        int q = std::min(m.quads(l), MAX_QUADS);
        s.first[l] = first; s.count[l] = q;
        all.insert(all.end(), m.data[l].begin(), m.data[l].begin() + (size_t)q * 12);
        first += q;
    }
    sg_buffer_desc d = {}; d.usage.vertex_buffer = true; d.data = {all.data(), all.size() * 4}; d.label = "section";
    s.buf = sg_make_buffer(&d);
    s.bytes = all.size() * 4;
    Profiler::add(Stat::MeshBytesUploaded, s.bytes);
    stats_.uploadMs += sw.ms();
}

void Renderer::updateMeshes(Vec3 cam, int radius) {
    renderRadius = radius;
    if (!world_) return;
    struct Cand { ChunkPos p; float d; };
    std::vector<Cand> cands;
    int inflight = 0, dirty = 0;
    for (auto& [p, c] : chunks_) {
        if (c.inflight) { inflight++; continue; }
        if (!c.dirty) continue;
        dirty++;
        float dx = p.x * 16 + 8 - cam.x, dz = p.z * 16 + 8 - cam.z;
        if (dx * dx + dz * dz > (float)((radius + 1) * 16) * (float)((radius + 1) * 16)) continue;
        cands.push_back({p, dx * dx + dz * dz});
    }
    stats_.meshJobsInFlight = inflight; stats_.dirtyChunks = dirty;
    int budget = std::max(2, jobs_->workerCount() * 2) - inflight;
    if (budget <= 0 || cands.empty()) return;
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
    for (const Cand& cd : cands) {
        if (budget <= 0) break;
        bool ready = true;
        for (int dz = -1; dz <= 1 && ready; dz++) for (int dx = -1; dx <= 1; dx++) if (!world_->isReady({cd.p.x + dx, cd.p.z + dz})) { ready = false; break; }
        if (!ready) continue;
        auto snap = std::make_shared<MeshSnapshot>();
        if (!buildMeshSnapshot(*world_, cd.p, *snap)) continue;
        ChunkGpu& c = chunks_[cd.p];
        u16 mask = c.dirty; c.dirty = 0; c.inflight = true; u32 gen = ++c.gen;
        budget--;
        ChunkPos p = cd.p; auto alive = alive_; MeshContext ctx = mctx_; JobSystem* js = jobs_;
        jobs_->submit([this, p, gen, mask, snap, alive, ctx, js] {
            auto meshes = std::make_shared<std::vector<std::pair<int, SectionMesh>>>();
            for (int sy = 0; sy < SECTIONS; sy++) if (mask & (1u << sy)) { meshes->emplace_back(sy, SectionMesh{}); meshSection(*snap, sy, ctx, meshes->back().second); }
            js->postMain([this, p, gen, meshes, alive] {
                if (!alive->load()) return;
                auto it = chunks_.find(p);
                if (it == chunks_.end() || it->second.gen != gen) return;
                for (auto& [sy, m] : *meshes) upload(p, gen, sy, m);
                it->second.inflight = false;
            });
        });
    }
}

void Renderer::render(Vec3 eye, float yaw, float pitch, int width, int height, const FrameEnv& env, const RayHit* outline, const Ui& ui,
                      const std::vector<EntityVertex>* entities) {
    stats_.drawCalls = 0; stats_.quads = 0; stats_.visibleSections = 0; stats_.sections = 0; stats_.gpuBytes = 0;
    Mat4 proj = Mat4::perspectiveRevZ(env.fovY, (float)width / (float)std::max(1, height), 0.05f);
    Mat4 vp = proj * viewMatrix(yaw, pitch);
    Frustum fr; fr.build(vp);
    lastFrustum = fr;

    struct Item { const SectionGpu* s; Vec3 origin; float d; };
    std::vector<Item> items; items.reserve(chunks_.size() * 4);
    float maxD = (float)(renderRadius * 16 + 16);
    for (auto& [p, c] : chunks_) for (auto& s : c.sec) if (s.buf.id) { stats_.sections++; stats_.gpuBytes += s.bytes; }
    if (occlusionCulling) {
        SectionKey cam{(int)std::floor(eye.x) >> 4, clamp((int)std::floor(eye.y) >> 4, 0, SECTIONS - 1), (int)std::floor(eye.z) >> 4};
        traverseVisible(cam, renderRadius + 1,
            [&](const SectionKey& k) -> FaceMask { auto it = chunks_.find({k.x, k.z}); return it == chunks_.end() ? ALL_CONNECTED : it->second.sec[k.y].conn; },
            [&](const SectionKey& k) {
                Vec3 o{(float)(k.x * 16) - eye.x, (float)(k.y * 16) - eye.y, (float)(k.z * 16) - eye.z};
                if ((o.x + 8) * (o.x + 8) + (o.z + 8) * (o.z + 8) > maxD * maxD) return false;
                return fr.visible({o, o + Vec3(16, 16, 16)});
            },
            [&](const SectionKey& k) {
                auto it = chunks_.find({k.x, k.z});
                if (it == chunks_.end() || !it->second.sec[k.y].buf.id) return;
                Vec3 o{(float)(k.x * 16) - eye.x, (float)(k.y * 16) - eye.y, (float)(k.z * 16) - eye.z};
                Vec3 cc = o + Vec3(8, 8, 8);
                items.push_back({&it->second.sec[k.y], o, dot(cc, cc)});
            });
    } else for (auto& [p, c] : chunks_) {
        float ox = (float)(p.x * 16) - eye.x, oz = (float)(p.z * 16) - eye.z;
        if ((ox + 8) * (ox + 8) + (oz + 8) * (oz + 8) > maxD * maxD) continue;
        for (int sy = 0; sy < SECTIONS; sy++) {
            const SectionGpu& s = c.sec[sy];
            if (!s.buf.id) continue;
            Vec3 o{ox, (float)(sy * 16) - eye.y, oz};
            if (!fr.visible({o, o + Vec3(16, 16, 16)})) continue;
            Vec3 cc = o + Vec3(8, 8, 8);
            items.push_back({&s, o, dot(cc, cc)});
        }
    }
    stats_.visibleSections = (int)items.size();
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.d < b.d; });

    sg_pass pass = {};
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {env.skyColor.x, env.skyColor.y, env.skyColor.z, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR; pass.action.depth.clear_value = 0.0f;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    struct { Mat4 vp; float cam[4]; } frameU{vp, {0, 0, 0, 0}};
    struct { float fog[4]; float params[4]; } fogU{{env.skyColor.x, env.skyColor.y, env.skyColor.z, 1}, {env.daylight, env.fogStart, env.fogEnd, env.ambient}};
    auto drawLayer = [&](sg_pipeline pip, int layer, bool backToFront) {
        sg_apply_pipeline(pip);
        sg_apply_uniforms(0, SG_RANGE_REF(frameU));
        sg_apply_uniforms(2, SG_RANGE_REF(fogU));
        auto one = [&](const Item& it) {
            const SectionGpu& s = *it.s;
            if (!s.count[layer]) return;
            sg_bindings b = {};
            b.vertex_buffers[0] = s.buf; b.vertex_buffer_offsets[0] = s.first[layer] * 4 * VERTEX_BYTES;
            b.index_buffer = quadIdx_;
            b.views[0] = blockView_; b.samplers[0] = blockSmp_;
            sg_apply_bindings(&b);
            float o[4] = {it.origin.x, it.origin.y, it.origin.z, 0};
            sg_apply_uniforms(1, SG_RANGE_REF(o));
            sg_draw(0, s.count[layer] * 6, 1);
            stats_.drawCalls++; stats_.quads += (u64)s.count[layer];
        };
        if (backToFront) for (auto i = items.rbegin(); i != items.rend(); ++i) one(*i);
        else for (auto& i : items) one(i);
    };
    drawLayer(opaquePip_, ML_OPAQUE, false);
    drawLayer(cutoutPip_, ML_CUTOUT, false);
    if (entities && !entities->empty()) {
        size_t n = std::min(entities->size() / 4 * 4, entityCapacity_);
        sg_write_buffer_desc wd = {}; wd.src.data = {entities->data(), n * sizeof(EntityVertex)}; wd.dst.buffer = entityBuf_;
        sg_write_buffer_transient(&wd);
        sg_apply_pipeline(entityPip_);
        sg_apply_uniforms(0, SG_RANGE_REF(frameU));
        sg_apply_uniforms(2, SG_RANGE_REF(fogU));
        sg_bindings b = {}; b.vertex_buffers[0] = entityBuf_; b.index_buffer = quadIdx_; b.views[0] = blockView_; b.samplers[0] = blockSmp_;
        sg_apply_bindings(&b);
        sg_draw(0, (int)(n / 4 * 6), 1);
        stats_.drawCalls++; stats_.quads += n / 4;
    }
    drawLayer(translucentPip_, ML_TRANSLUCENT, true);

    if (outline && outline->hit) {
        sg_apply_pipeline(linesPip_);
        sg_apply_uniforms(0, SG_RANGE_REF(frameU));
        // outline box: the block's shape bounds (1/16 units), not always the full cube
        const AABB& ob = outlineBox;
        float o[8] = {(float)outline->block.x + ob.min.x - eye.x, (float)outline->block.y + ob.min.y - eye.y, (float)outline->block.z + ob.min.z - eye.z, 0,
                      ob.max.x - ob.min.x, ob.max.y - ob.min.y, ob.max.z - ob.min.z, 0};
        sg_apply_uniforms(1, {o, sizeof o});
        float col[4] = {0.05f, 0.05f, 0.05f, 0.7f};
        sg_apply_uniforms(2, SG_RANGE_REF(col));
        sg_bindings b = {}; b.vertex_buffers[0] = outlineBuf_;
        sg_apply_bindings(&b);
        sg_draw(0, 24, 1);
        stats_.drawCalls++;
    }

    if (!ui.verts.empty()) {
        size_t n = std::min(ui.verts.size(), uiCapacity_);
        sg_write_buffer_desc wd = {};
        wd.src.data = {ui.verts.data(), n * sizeof(UiVertex)}; wd.dst.buffer = uiBuf_;
        sg_write_buffer_transient(&wd);
        sg_apply_pipeline(uiPip_);
        sg_bindings b = {}; b.vertex_buffers[0] = uiBuf_;
        b.views[0] = fontView_; b.views[1] = blockView_; b.samplers[0] = uiSmp_;
        sg_apply_bindings(&b);
        float sz[4] = {(float)width, (float)height, 0, 0};
        sg_apply_uniforms(0, SG_RANGE_REF(sz));
        sg_draw(0, (int)n, 1);
        stats_.drawCalls++;
    }
    sg_end_pass();
    sg_commit();
    Profiler::set(Stat::DrawCalls, (u64)stats_.drawCalls);
    Profiler::set(Stat::Triangles, stats_.quads * 2);
}

} // namespace vx
