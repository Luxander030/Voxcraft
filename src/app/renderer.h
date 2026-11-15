// GPU renderer (sokol_gfx: Metal on macOS, D3D11 on Windows).
//
// Section meshes are built on worker threads from immutable snapshots (render/mesher.h) and
// uploaded on the main thread as one immutable vertex buffer per non-empty section, holding the
// opaque, cutout and translucent ranges back to back. Only sections flagged dirty by the world are
// re-meshed; a single block edit re-meshes 1-3 sections, never the whole chunk.
//
// Per frame: frustum culling per section, opaque front-to-back, cutout, translucent back-to-front,
// block outline, then the UI in one draw call. Rendering is camera-relative (section origins are
// sent relative to the eye) so precision does not degrade far from the world origin.
#pragma once
#include "app/ui.h"
#include "core/jobs.h"
#include "game/player.h"
#include "render/mesher.h"
#include "world/world.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_glue.h"
#include <unordered_map>

namespace vx {

struct EntityVertex { float x, y, z; float u, v, layer, shade; float sky, block; u32 tint; };   // 40 bytes

struct FrameEnv {
    float daylight = 1.0f;       // 0 night .. 1 noon (scales sky light)
    Vec3 skyColor{0.62f, 0.78f, 1.0f};
    float fogStart = 100, fogEnd = 160;
    float fovY = 70.0f * PI / 180.0f;
    float ambient = 0.0f;   // minimum light (dimensions without sky)
};

class Renderer final : public WorldListener {
public:
    struct Stats { int sections = 0, visibleSections = 0, drawCalls = 0, meshJobsInFlight = 0, dirtyChunks = 0; u64 quads = 0; size_t gpuBytes = 0; double uploadMs = 0; };

    bool init(const BlockRegistry& reg, const TexturePack& pack, JobSystem& jobs, std::string* err);
    void shutdown();
    void attach(World* w) { world_ = w; if (w) w->setListener(this); }
    void detach();
    // Schedule mesh jobs for dirty sections near the camera.
    void updateMeshes(Vec3 cam, int renderRadius);
    void render(Vec3 eye, float yaw, float pitch, int width, int height, const FrameEnv& env, const RayHit* outline, const Ui& ui,
                const std::vector<EntityVertex>* entities = nullptr);
    Frustum lastFrustum;   // camera-relative, from the previous render (entity culling)
    AABB outlineBox{{0, 0, 0}, {1, 1, 1}};   // block-local bounds of the outlined block
    const Stats& stats() const { return stats_; }
    const BlockTextures& textures() const { return tex_; }
    int renderRadius = 10;
    bool occlusionCulling = true;   // cave culling via the section connectivity graph

    // WorldListener
    void onChunkReady(ChunkPos p) override;
    void onSectionsChanged(ChunkPos p, u16 mask) override;
    void onChunkUnloaded(ChunkPos p) override;

private:
    struct SectionGpu { sg_buffer buf{}; int first[ML_COUNT] = {}; int count[ML_COUNT] = {}; size_t bytes = 0; u16 conn = 0x7FFF; };
    struct ChunkGpu { SectionGpu sec[SECTIONS]; u16 dirty = 0; bool inflight = false; u32 gen = 0; };
    void upload(ChunkPos p, u32 gen, int sy, const SectionMesh& m);
    void freeSection(SectionGpu& s);
    void markDirty(ChunkPos p, u16 mask);

    const BlockRegistry* reg_ = nullptr;
    JobSystem* jobs_ = nullptr;
    World* world_ = nullptr;
    BlockTextures tex_;
    MeshContext mctx_;
    std::unordered_map<ChunkPos, ChunkGpu, ChunkPosHash> chunks_;
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    sg_image blockImg_{}, fontImg_{};
    sg_view blockView_{}, fontView_{};
    sg_sampler blockSmp_{}, uiSmp_{};
    sg_shader terrainOpaqueSh_{}, terrainCutoutSh_{}, linesSh_{}, uiSh_{}, entitySh_{};
    sg_pipeline opaquePip_{}, cutoutPip_{}, translucentPip_{}, linesPip_{}, uiPip_{}, entityPip_{};
    sg_buffer quadIdx_{}, outlineBuf_{}, uiBuf_{}, entityBuf_{};
    size_t uiCapacity_ = 0, entityCapacity_ = 0;
    Stats stats_;
};

} // namespace vx
