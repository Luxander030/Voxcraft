// Voxcraft entry point: window, input, fixed-step simulation, streaming, rendering, HUD.
//
// Command line:
//   --world <name>   world folder under <user data>/Voxcraft/saves (default "World1")
//   --seed <n>       seed for a new world        --flat     flat preset for a new world
//   --radius <n>     render distance in chunks   --smoke <frames>  scripted run, print stats, exit
#include "app/renderer.h"
#include "app/ui.h"
#include "core/profiler.h"
#include "game/player.h"
#include "world/world.h"
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_log.h"
#include "sokol/sokol_time.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace vx;

extern "C" int vx_platform_window_id(void);
extern "C" void vx_platform_activate(void);

namespace {

struct Options {
    std::string world = "World1";
    u64 seed = 0; bool seedSet = false, flat = false;
    int radius = 10;
    int smokeFrames = 0;
    std::string screenshot;   // smoke mode: capture this window only (macOS screencapture -l)
};

struct Game {
    Options opt;
    fs::path assetDir, saveDir;
    BlockRegistry reg;
    TexturePack pack;
    std::unique_ptr<JobSystem> jobs;
    std::unique_ptr<World> world;
    Renderer renderer;
    Ui ui;
    Player player;
    PlayerInput input;
    bool keys[512] = {};
    bool mouseLocked = false, paused = false, debug = false, quitting = false;
    double accumulator = 0, lastSave = 0, now = 0;
    u64 frame = 0;
    float frameMsAvg = 16, frameMsMax = 0, fps = 60;
    RayHit target;
    int hotbarSel = 0;
    std::vector<BlockState> hotbar;
    bool playerReady = false;
    std::string status;
    double statusUntil = 0;
};
Game* G = nullptr;

ItemNames blockItems(const BlockRegistry& reg) {
    // Until the item registry exists (Phase 6) container items are block names.
    return {[&reg](u16 id) { return id < reg.blockCount() ? reg.defs()[id].name : std::string(); },
            [&reg](const std::string& n) { const BlockDef* d = reg.byName(n); return d ? d->index : (u16)0; }};
}

fs::path findAssets() {
    fs::path exe = executableDir();
    for (fs::path c : {exe / ".." / "Resources", exe, fs::path(VX_SOURCE_DIR) / "assets" / "generated"}) {
        std::error_code ec;
        if (fs::exists(c / "voxcraft.pak", ec)) return c;
    }
    return {};
}

std::vector<u8> encodePlayer(const Player& p, int sel) {
    ByteWriter w; w.u8_(1);
    w.f64_(p.pos.x); w.f64_(p.pos.y); w.f64_(p.pos.z); w.f32_(p.yaw); w.f32_(p.pitch); w.u8_(p.flying ? 1 : 0); w.u8_((u8)sel);
    return std::move(w.buf);
}
bool decodePlayer(const std::vector<u8>& b, Player& p, int& sel) {
    ByteReader r(b); if (r.u8_() != 1) return false;
    p.pos.x = (float)r.f64_(); p.pos.y = (float)r.f64_(); p.pos.z = (float)r.f64_(); p.yaw = r.f32_(); p.pitch = r.f32_();
    p.flying = r.u8_() != 0; sel = r.u8_();
    return r.ok();
}

bool saveWorld(bool sync) {
    std::string err;
    std::map<std::string, std::vector<u8>> extras{{"player.dat", encodePlayer(G->player, G->hotbarSel)}};
    if (!G->world->save(sync, extras, &err)) { VX_ERROR("save failed: %s", err.c_str()); return false; }
    return true;
}

void showStatus(const std::string& s) { G->status = s; G->statusUntil = G->now + 3; }

void setMouseLock(bool lock) { G->mouseLocked = lock; sapp_lock_mouse(lock); G->paused = !lock; }

// ------------------------------------------------------------------ interaction
int horizontalFacingToward(const Player& p) {   // front of the block faces the player
    Vec3 l = p.look();
    if (std::fabs(l.x) > std::fabs(l.z)) return l.x > 0 ? 2 : 3;   // looking +X -> front faces W (2), else E (3)
    return l.z > 0 ? 0 : 1;                                              // looking +Z -> front faces N (0), else S (1)
}

void breakBlock() {
    if (!G->target.hit) return;
    const BlockDef& d = G->reg.def(G->world->getBlock(G->target.block));
    if (d.hardness < 0) return;   // unbreakable
    G->world->setBlock(G->target.block, AIR);
}

void placeBlock() {
    if (!G->target.hit || G->hotbar.empty()) return;
    BlockState place = G->hotbar[G->hotbarSel];
    BlockPos at = G->target.block;
    BlockState cur = G->world->getBlock(at);
    if (!G->reg.def(cur).has(BF_REPLACEABLE)) at = at + G->target.normal;
    BlockState there = G->world->getBlock(at);
    if (!G->reg.def(there).has(BF_REPLACEABLE)) return;
    const BlockDef& d = G->reg.def(place);
    if (d.solid) {
        AABB bb{{(float)at.x, (float)at.y, (float)at.z}, {(float)at.x + 1, (float)at.y + 1, (float)at.z + 1}};
        if (bb.intersects(G->player.box())) return;
    }
    if (d.propIndex("facing") >= 0) {
        int cnt = d.props[d.propIndex("facing")].count;
        const BlockPos& n = G->target.normal;
        if (d.shape == BlockShape::Torch) {
            if (n.y < 0) return;
            int f = n.y > 0 ? 4 : n.z < 0 ? 0 : n.z > 0 ? 1 : n.x < 0 ? 2 : 3;
            place = G->reg.withProp(place, "facing", f);
        } else if (d.shape == BlockShape::FlatWall) {
            if (n.y != 0) return;
            place = G->reg.withProp(place, "facing", n.z < 0 ? 0 : n.z > 0 ? 1 : n.x < 0 ? 2 : 3);
        } else if (cnt == 4) {
            place = G->reg.withProp(place, "facing", horizontalFacingToward(G->player));
        } else if (cnt == 6) {
            Vec3 l = G->player.look(); int f;
            if (std::fabs(l.y) > 0.7f) f = l.y > 0 ? 0 : 1; else { int h = horizontalFacingToward(G->player); f = h + 2; }
            place = G->reg.withProp(place, "facing", f);
        }
    }
    G->world->setBlock(at, place);
}

void pickBlock() {
    if (!G->target.hit) return;
    BlockState s = G->world->getBlock(G->target.block);
    BlockState base = G->reg.def(s).base;
    for (size_t i = 0; i < G->hotbar.size(); i++) if (G->hotbar[i] == base) { G->hotbarSel = (int)i; return; }
    G->hotbar[G->hotbarSel] = base;
}

// ------------------------------------------------------------------ HUD
const char* facingName(float yaw) {
    float d = std::fmod(yaw * 180.0f / PI + 360.0f * 4, 360.0f);
    if (d < 45 || d >= 315) return "north (-Z)"; if (d < 135) return "east (+X)"; if (d < 225) return "south (+Z)"; return "west (-X)";
}

void drawHud(int w, int h) {
    float gs = std::max(1.0f, std::floor(std::min(w / 320.0f, h / 240.0f)));
    Ui& ui = G->ui; ui.begin((float)w, (float)h, gs);
    float W = w / gs, H = h / gs;
    // crosshair
    ui.rect(W / 2 - 4.5f, H / 2 - 0.5f, 9, 1, rgba(240, 240, 240, 200));
    ui.rect(W / 2 - 0.5f, H / 2 - 4.5f, 1, 9, rgba(240, 240, 240, 200));
    // hotbar
    float slot = 20, bw = slot * 9 + 2, bx = W / 2 - bw / 2, by = H - slot - 3;
    ui.rect(bx, by, bw, slot + 2, rgba(20, 20, 24, 160));
    for (int i = 0; i < 9 && i < (int)G->hotbar.size(); i++) {
        float x = bx + 1 + i * slot;
        ui.frame(x, by + 1, slot, slot, 1, rgba(90, 90, 100, 220));
        ui.blockIcon(x + slot / 2, by + 1 + slot / 2, 13, G->hotbar[i], G->reg, G->renderer.textures());
    }
    ui.frame(bx + G->hotbarSel * slot, by, slot + 2, slot + 2, 1.5f, rgba(250, 250, 250, 255));
    const std::string& sel = G->reg.def(G->hotbar[G->hotbarSel]).name;
    ui.text(W / 2 - Ui::textWidth(sel) / 2, by - 11, sel, rgba(255, 255, 255, 230));
    if (G->now < G->statusUntil) ui.text(W / 2 - Ui::textWidth(G->status) / 2, H / 2 + 20, G->status, rgba(255, 240, 160));
    if (G->paused && !G->quitting) {
        ui.rect(0, 0, W, H, rgba(0, 0, 0, 110));
        const char* t1 = "Paused"; const char* t2 = "Click to resume - Esc again to save and quit";
        ui.text(W / 2 - Ui::textWidth(t1) / 2, H / 2 - 20, t1, rgba(255, 255, 255));
        ui.text(W / 2 - Ui::textWidth(t2) / 2, H / 2 - 6, t2, rgba(200, 200, 200));
    }
    if (!G->playerReady) { const char* t = "Generating terrain..."; ui.text(W / 2 - Ui::textWidth(t) / 2, H / 2 - 30, t, rgba(255, 255, 255)); }

    if (!G->debug) return;
    char buf[256]; float y = 2;
    auto line = [&](const char* fmt, auto... args) { snprintf(buf, sizeof buf, fmt, args...); ui.rect(1, y - 1, Ui::textWidth(buf) + 2, 9, rgba(0, 0, 0, 120)); ui.text(2, y, buf, rgba(230, 230, 230)); y += 9; };
    const Player& p = G->player;
    auto ws = G->world->stats(); auto js = G->jobs->stats(); auto rs = G->renderer.stats();
#if defined(SOKOL_METAL)
    const char* backend = "Metal";
#else
    const char* backend = "D3D11";
#endif
    line("Voxcraft 0.1.0 (%s)  %.0f fps  %.2f ms avg  %.1f ms max", backend, G->fps, G->frameMsAvg, G->frameMsMax);
    line("XYZ %.2f / %.2f / %.2f   chunk %d %d   facing %s", p.pos.x, p.pos.y, p.pos.z, (int)std::floor(p.pos.x) >> 4, (int)std::floor(p.pos.z) >> 4, facingName(p.yaw));
    BlockPos bp{(int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)};
    ColumnInfo col = G->world->generator().column(bp.x, bp.z);
    line("biome %s   light sky %d block %d   %s%s", biomeName(col.biome), G->world->skyLight(bp), G->world->blockLight(bp), p.flying ? "flying " : "", p.onGround ? "ground" : "");
    i64 dt = G->world->meta.dayTime; int hh = (int)((dt / 1000 + 6) % 24), mm = (int)(dt % 1000 * 60 / 1000);
    line("day %lld  %02d:%02d   seed %llu", (long long)(G->world->meta.tick / 24000), hh, mm, (unsigned long long)G->world->meta.gen.seed);
    line("chunks ready %d  gen %d  cached %d  modified %d  pending writes %d", ws.ready, ws.generating + ws.requested, ws.cached, ws.withData, ws.pendingWrites);
    line("jobs queued %d  running %d/%d  mesh in flight %d  dirty %d", js.queued, js.running, js.workers, rs.meshJobsInFlight, rs.dirtyChunks);
    line("render %d/%d sections  %d draws  %llu tris  mesh VRAM %.1f MiB", rs.visibleSections, rs.sections, rs.drawCalls, (unsigned long long)(rs.quads * 2), rs.gpuBytes / 1048576.0);
    auto avg = [](Stat t, Stat n) { u64 c = Profiler::get(n); return c ? Profiler::get(t) / 1000.0 / (double)c : 0.0; };
    line("avg ms: gen %.2f  light %.2f  mesh/section %.3f   save %.1f total", avg(Stat::GenTimeUs, Stat::ChunksGenerated), avg(Stat::LightTimeUs, Stat::ChunksLit),
         avg(Stat::MeshTimeUs, Stat::ChunksMeshed), Profiler::get(Stat::SaveTimeUs) / 1000.0);
    line("memory rss %.0f MiB  chunks %.1f MiB  cache %.1f MiB", processMemoryBytes() / 1048576.0, ws.chunkBytes / 1048576.0, ws.cacheBytes / 1048576.0);
    if (G->target.hit) {
        BlockState s = G->world->getBlock(G->target.block);
        line("target %s at %d %d %d", G->reg.stateString(s).c_str(), G->target.block.x, G->target.block.y, G->target.block.z);
    }
}

// ------------------------------------------------------------------ callbacks
void init() {
    sg_desc d = {};
    d.environment = sglue_environment(); d.logger.func = slog_func;
    d.buffer_pool_size = 16384; d.view_pool_size = 64; d.uniform_buffer_size = 16 << 20;
    sg_setup(&d);

    std::string err;
    G->assetDir = findAssets();
    if (G->assetDir.empty()) { VX_ERROR("assets not found (voxcraft.pak)"); sapp_request_quit(); return; }
    fs::path blocksJson = G->assetDir / "blocks.json";
    if (!fs::exists(blocksJson)) blocksJson = fs::path(VX_SOURCE_DIR) / "assets" / "data" / "blocks.json";
    if (!G->reg.loadFile(blocksJson.string(), &err) || !G->pack.load((G->assetDir / "voxcraft.pak").string(), &err)) {
        VX_ERROR("asset load failed: %s", err.c_str()); sapp_request_quit(); return;
    }
    G->jobs = std::make_unique<JobSystem>(0);
    if (!G->renderer.init(G->reg, G->pack, *G->jobs, &err)) { VX_ERROR("renderer: %s", err.c_str()); sapp_request_quit(); return; }

    G->world = std::make_unique<World>(G->reg, *G->jobs, blockItems(G->reg));
    G->saveDir = userDataDir("Voxcraft") / "saves" / G->opt.world;
    bool exists = fs::exists(G->saveDir / "world.json");
    if (exists) {
        if (!G->world->open(G->saveDir, &err)) { VX_ERROR("open world: %s", err.c_str()); sapp_request_quit(); return; }
        if (auto pd = G->world->storage().readExtra("player.dat"); pd && decodePlayer(*pd, G->player, G->hotbarSel)) {}
        else G->player.pos = {G->world->meta.spawn.x + 0.5f, (float)G->world->meta.spawn.y, G->world->meta.spawn.z + 0.5f};
        VX_INFO("opened world %s", G->saveDir.string().c_str());
    } else {
        WorldMeta m; m.name = G->opt.world;
        m.gen.seed = G->opt.seedSet ? G->opt.seed : (u64)time(nullptr) * 0x9E3779B97F4A7C15ull;
        if (G->opt.flat) m.gen.preset = "flat";
        m.createdUnix = (u64)time(nullptr);
        auto gen = createGenerator(m.gen, G->reg);
        ColumnInfo c = gen->column(8, 8);
        m.spawn = {8, std::max(c.height, SEA_LEVEL) + 1, 8};
        if (!G->world->create(G->saveDir, m, &err)) { VX_ERROR("create world: %s", err.c_str()); sapp_request_quit(); return; }
        G->player.pos = {8.5f, (float)m.spawn.y, 8.5f};
        VX_INFO("created world %s (seed %llu)", G->saveDir.string().c_str(), (unsigned long long)m.gen.seed);
    }
    G->player.prevPos = G->player.pos;
    G->renderer.attach(G->world.get());
    for (const char* n : {"stone", "cobblestone", "oak_planks", "glass", "bricks", "oak_log", "torch", "glowcrystal", "sand"})
        G->hotbar.push_back(G->reg.stateOf(n));
    G->hotbarSel = clamp(G->hotbarSel, 0, 8);
    G->lastSave = stm_sec(stm_now());
}

void frame() {
    if (!G->world) { sg_pass ps = {}; ps.swapchain = sglue_swapchain(); sg_begin_pass(&ps); sg_end_pass(); sg_commit(); return; }
    double dt = std::min(sapp_frame_duration(), 0.25);
    G->now += dt;
    float ms = (float)(dt * 1000.0);
    G->frameMsAvg = G->frameMsAvg * 0.95f + ms * 0.05f; G->fps = 1000.0f / std::max(0.1f, G->frameMsAvg);
    G->frameMsMax = std::max(G->frameMsMax * 0.995f, ms);
    G->frame++;

    if (G->opt.smokeFrames) {   // scripted: look around, fly forward, place and break
        G->player.yaw += 0.01f; G->input.forward = G->frame > 120 ? 1.0f : 0.0f; G->player.flying = true; G->playerReady = true;
        if (G->frame == 200) { placeBlock(); breakBlock(); }
    }

    Player& p = G->player;
    ChunkPos pc{(int)std::floor(p.pos.x) >> 4, (int)std::floor(p.pos.z) >> 4};
    if (!G->playerReady && G->world->isReady(pc)) {
        // place the player on the surface if the stored position is inside terrain
        while (p.pos.y < WORLD_H - 2 && G->reg.solid(G->world->getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)))) p.pos.y += 1;
        p.prevPos = p.pos;
        G->playerReady = true;
    }
    bool simulate = G->playerReady && G->world->isReady(pc) && (!G->paused || G->opt.smokeFrames);
    G->accumulator += dt;
    int steps = 0;
    while (G->accumulator >= 0.05 && steps < 5) {
        G->accumulator -= 0.05; steps++;
        if (!simulate) continue;
        PlayerInput in = G->input;
        in.forward = (G->keys[SAPP_KEYCODE_W] ? 1.f : 0.f) - (G->keys[SAPP_KEYCODE_S] ? 1.f : 0.f) + G->input.forward;
        in.strafe = (G->keys[SAPP_KEYCODE_D] ? 1.f : 0.f) - (G->keys[SAPP_KEYCODE_A] ? 1.f : 0.f);
        in.jump = G->keys[SAPP_KEYCODE_SPACE]; in.sneak = G->keys[SAPP_KEYCODE_LEFT_SHIFT];
        in.sprint = G->keys[SAPP_KEYCODE_LEFT_CONTROL] || G->keys[SAPP_KEYCODE_R];
        p.tick(*G->world, in);
        G->input.toggleFly = false;
        G->world->tick();
    }
    if (steps == 5) G->accumulator = 0;   // fell far behind (e.g. window dragged): drop time rather than spiral

    float alpha = (float)(G->accumulator / 0.05);
    Vec3 renderPos = G->player.prevPos + (p.pos - p.prevPos) * alpha;
    Vec3 eye{renderPos.x, renderPos.y + p.eyeHeight(), renderPos.z};
    G->target = raycastBlocks(*G->world, eye, p.look(), 5.0f);

    G->world->setFocus(renderPos, G->opt.radius + 2);
    G->world->update(4.0);
    G->renderer.updateMeshes(eye, G->opt.radius);

    if (G->now - G->lastSave > 45.0) { saveWorld(false); G->lastSave = G->now; }

    // environment: sun height from time of day (6000 = noon)
    float ang = (float)(G->world->meta.dayTime - 6000) / 24000.0f * 2 * PI;
    float daylight = clamp(0.5f + std::cos(ang) * 0.9f, 0.12f, 1.0f);
    FrameEnv env;
    env.daylight = daylight;
    Vec3 day{0.62f, 0.78f, 1.0f}, night{0.02f, 0.03f, 0.07f};
    env.skyColor = night + (day - night) * ((daylight - 0.12f) / 0.88f);
    env.fogEnd = (float)(G->opt.radius * 16); env.fogStart = env.fogEnd * 0.6f;
    BlockState headBlock = G->world->getBlock((int)std::floor(eye.x), (int)std::floor(eye.y), (int)std::floor(eye.z));
    if (G->reg.fluid(headBlock)) {
        bool lava = G->reg.def(headBlock).name == "lava";
        env.skyColor = lava ? Vec3{0.6f, 0.15f, 0.02f} : Vec3{0.05f, 0.15f, 0.4f} * std::max(0.3f, daylight);
        env.fogStart = 0; env.fogEnd = lava ? 2.0f : 24.0f;
    }

    drawHud(sapp_width(), sapp_height());
    G->renderer.render(eye, p.yaw, p.pitch, sapp_width(), sapp_height(), env, &G->target, G->ui);

    if (G->opt.smokeFrames && G->frame == 2) vx_platform_activate();
    if (!G->opt.screenshot.empty() && (int)G->frame == G->opt.smokeFrames - 5) {
#if defined(__APPLE__)
        int id = vx_platform_window_id();
        if (id) { std::string cmd = "screencapture -x -o -l" + std::to_string(id) + " '" + G->opt.screenshot + "'"; if (std::system(cmd.c_str()) != 0) VX_WARN("screenshot failed"); }
#endif
    }
    if (G->opt.smokeFrames && (int)G->frame >= G->opt.smokeFrames) {
        auto rs = G->renderer.stats(); auto ws = G->world->stats();
        printf("smoke: %llu frames, avg %.2f ms, chunks ready %d, sections %d visible %d, draws %d, tris %llu, mesh VRAM %.1f MiB, rss %.0f MiB\n",
               (unsigned long long)G->frame, G->frameMsAvg, ws.ready, rs.sections, rs.visibleSections, rs.drawCalls, (unsigned long long)(rs.quads * 2),
               rs.gpuBytes / 1048576.0, processMemoryBytes() / 1048576.0);
        sapp_request_quit();
    }
}

void cleanup() {
    if (G->world) {
        G->renderer.shutdown();
        saveWorld(true);
        G->world->close();
        G->world.reset();
    }
    sg_shutdown();
    G->jobs.reset();
    delete G; G = nullptr;
}

void event(const sapp_event* e) {
    switch (e->type) {
    case SAPP_EVENTTYPE_KEY_DOWN:
        if (e->key_code >= 0 && e->key_code < 512) G->keys[e->key_code] = true;
        if (e->key_code == SAPP_KEYCODE_ESCAPE) {
            if (G->mouseLocked) setMouseLock(false);
            else if (G->paused && G->playerReady) { G->quitting = true; sapp_request_quit(); }
        }
        if (e->key_code == SAPP_KEYCODE_F3) G->debug = !G->debug;
        if (e->key_code == SAPP_KEYCODE_F11) sapp_toggle_fullscreen();
        if (e->key_code == SAPP_KEYCODE_F && !e->key_repeat) { G->input.toggleFly = true; }
        if (e->key_code == SAPP_KEYCODE_F5 && !e->key_repeat) { if (saveWorld(true)) showStatus("World saved"); }
        if (e->key_code >= SAPP_KEYCODE_1 && e->key_code <= SAPP_KEYCODE_9) G->hotbarSel = e->key_code - SAPP_KEYCODE_1;
        break;
    case SAPP_EVENTTYPE_KEY_UP:
        if (e->key_code >= 0 && e->key_code < 512) G->keys[e->key_code] = false;
        break;
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (!G->mouseLocked) { setMouseLock(true); break; }
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) breakBlock();
        else if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) placeBlock();
        else if (e->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) pickBlock();
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (G->mouseLocked) {
            const float sens = 0.0025f;
            G->player.yaw += e->mouse_dx * sens;
            G->player.pitch = clamp(G->player.pitch - e->mouse_dy * sens, -1.55f, 1.55f);
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        if (e->scroll_y != 0) G->hotbarSel = (G->hotbarSel + (e->scroll_y < 0 ? 1 : 8)) % 9;
        break;
    case SAPP_EVENTTYPE_UNFOCUSED:
        if (G->mouseLocked) setMouseLock(false);
        for (bool& k : G->keys) k = false;
        break;
    case SAPP_EVENTTYPE_QUIT_REQUESTED:
        break;
    default: break;
    }
}

} // namespace

sapp_desc sokol_main(int argc, char* argv[]) {
    G = new Game();
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--world") G->opt.world = next();
        else if (a == "--seed") { G->opt.seed = std::strtoull(next(), nullptr, 10); G->opt.seedSet = true; }
        else if (a == "--flat") G->opt.flat = true;
        else if (a == "--radius") G->opt.radius = clamp(atoi(next()), 2, 32);
        else if (a == "--smoke") G->opt.smokeFrames = atoi(next());
        else if (a == "--screenshot") G->opt.screenshot = next();
    }
    logSetLevel(LogLevel::Info);
    fs::create_directories(userDataDir("Voxcraft"));
    logSetFile(userDataDir("Voxcraft") / "latest.log");
    stm_setup();
    G->paused = true;
    sapp_desc d = {};
    d.init_cb = init; d.frame_cb = frame; d.cleanup_cb = cleanup; d.event_cb = event;
    d.width = 1280; d.height = 720; d.high_dpi = true; d.window_title = "Voxcraft";
    d.sample_count = 1; d.swap_interval = 1;
    d.logger.func = slog_func;
    return d;
}
