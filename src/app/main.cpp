// Voxcraft entry point: sokol app callbacks, frame loop and input dispatch.
//
// Command line:
//   --world <name>   world folder under <user data>/Voxcraft/saves (default "World1")
//   --seed <n>       seed for a new world    --flat   flat preset    --creative   creative mode for a new world
//   --radius <n>     render distance in chunks
//   --smoke <frames> scripted run that prints stats and exits    --screenshot <png>  capture this window (macOS)
#include "app/game.h"
#include "core/profiler.h"
#include "sokol/sokol_log.h"
#include "sokol/sokol_time.h"
#include <cstdio>
#include <cstdlib>

using namespace vx;

extern "C" int vx_platform_window_id(void);
extern "C" void vx_platform_activate(void);

namespace {

fs::path findAssets() {
    fs::path exe = executableDir();
    for (fs::path c : {exe / ".." / "Resources", exe, fs::path(VX_SOURCE_DIR) / "assets" / "generated"}) {
        std::error_code ec;
        if (fs::exists(c / "voxcraft.pak", ec)) return c;
    }
    return {};
}

void init() {
    sg_desc d = {};
    d.environment = sglue_environment(); d.logger.func = slog_func;
    d.buffer_pool_size = 16384; d.view_pool_size = 64; d.uniform_buffer_size = 16 << 20;
    sg_setup(&d);
    G->assetDir = findAssets();
    std::string err;
    if (G->assetDir.empty()) err = "assets not found (voxcraft.pak)";
    else if (!startSession(&err)) {}
    if (!err.empty()) { VX_ERROR("startup failed: %s", err.c_str()); G->world.reset(); sapp_request_quit(); return; }
    G->lastSave = 0;
}

void environment(FrameEnv& env, const Vec3& eye) {
    float ang = (float)(G->world->meta.dayTime - 6000) / 24000.0f * 2 * PI;
    float daylight = clamp(0.5f + std::cos(ang) * 0.9f, 0.12f, 1.0f);
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
}

void frame() {
    if (!G->world) { sg_pass ps = {}; ps.swapchain = sglue_swapchain(); sg_begin_pass(&ps); sg_end_pass(); sg_commit(); return; }
    double dt = std::min(sapp_frame_duration(), 0.25);
    G->now += dt;
    float ms = (float)(dt * 1000.0);
    G->frameMsAvg = G->frameMsAvg * 0.95f + ms * 0.05f; G->fps = 1000.0f / std::max(0.1f, G->frameMsAvg);
    G->frameMsMax = std::max(G->frameMsMax * 0.995f, ms);
    G->frame++;

    if (G->opt.smokeFrames) {   // scripted: look around, fly forward, open the inventory near the end
        if (G->frame == 2) { vx_platform_activate(); G->screen = Screen::None; }
        G->player.yaw += 0.01f; G->input.forward = G->frame > 120 && G->frame < 400 ? 1.0f : 0.0f;
        if (G->creative()) G->player.flying = true;
        if ((int)G->frame == G->opt.smokeFrames - 30) openScreen(Screen::Inventory);
    }

    Player& p = G->player;
    ChunkPos pc{(int)std::floor(p.pos.x) >> 4, (int)std::floor(p.pos.z) >> 4};
    if (!G->playerReady && G->world->isReady(pc)) {
        while (p.pos.y < WORLD_H - 2 && G->reg.solid(G->world->getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)))) p.pos.y += 1;
        p.prevPos = p.pos;
        G->playerReady = true;
    }
    bool simulate = G->playerReady && G->world->isReady(pc) && G->screen != Screen::Pause;
    G->accumulator += dt;
    int steps = 0;
    while (G->accumulator >= 0.05 && steps < 5) {
        G->accumulator -= 0.05; steps++;
        if (simulate) simulateTick();
    }
    if (steps == 5) G->accumulator = 0;   // fell far behind: drop time rather than spiral

    float alpha = (float)(G->accumulator / 0.05);
    Vec3 renderPos = p.prevPos + (p.pos - p.prevPos) * alpha;
    Vec3 eye{renderPos.x, renderPos.y + p.eyeHeight(), renderPos.z};
    G->target = G->stats.dead ? RayHit{} : raycastBlocks(*G->world, eye, p.look(), G->creative() ? 6.0f : 5.0f);
    if (simulate) updateInteraction((float)dt);

    G->world->setFocus(renderPos, G->opt.radius + 2);
    G->world->update(4.0);
    G->renderer.updateMeshes(eye, G->opt.radius);
    if (G->now - G->lastSave > 45.0) { saveWorld(false); G->lastSave = G->now; }

    FrameEnv env; environment(env, eye);
    drawFrameUi(sapp_width(), sapp_height());
    G->renderer.render(eye, p.yaw, p.pitch, sapp_width(), sapp_height(), env, G->screen == Screen::None ? &G->target : nullptr, G->ui);

    if (!G->opt.screenshot.empty() && (int)G->frame == G->opt.smokeFrames - 5) {
#if defined(__APPLE__)
        if (int id = vx_platform_window_id()) {
            std::string cmd = "screencapture -x -o -l" + std::to_string(id) + " '" + G->opt.screenshot + "'";
            if (std::system(cmd.c_str()) != 0) VX_WARN("screenshot failed");
        }
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
    endSession();
    sg_shutdown();
    G->jobs.reset();
    delete G; G = nullptr;
}

void event(const sapp_event* e) {
    if (!G->world) return;
    bool inScreen = G->screen != Screen::None;
    switch (e->type) {
    case SAPP_EVENTTYPE_KEY_DOWN: {
        if (e->key_code >= 0 && e->key_code < 512 && !inScreen) G->keys[e->key_code] = true;
        sapp_keycode k = e->key_code;
        if (k == SAPP_KEYCODE_ESCAPE) {
            if (G->screen == Screen::None) { G->screen = Screen::Pause; G->mouseLocked = false; sapp_lock_mouse(false); for (bool& kk : G->keys) kk = false; }
            else if (G->screen == Screen::Pause) { if (G->playerReady) { G->quitting = true; sapp_request_quit(); } }
            else if (G->screen != Screen::Dead) closeScreen();
        }
        if (k == SAPP_KEYCODE_E && !e->key_repeat) {
            if (G->screen == Screen::None) openScreen(Screen::Inventory);
            else if (G->screen != Screen::Pause && G->screen != Screen::Dead) closeScreen();
        }
        if (k == SAPP_KEYCODE_F3) G->debug = !G->debug;
        if (k == SAPP_KEYCODE_F11) sapp_toggle_fullscreen();
        if (k == SAPP_KEYCODE_F5 && !e->key_repeat) { if (saveWorld(true)) showStatus("World saved"); }
        if (k == SAPP_KEYCODE_F4 && !e->key_repeat) {   // developer convenience: switch game mode
            G->world->meta.gameMode = G->creative() ? "survival" : "creative";
            G->player.canFly = G->creative(); if (!G->creative()) G->player.flying = false;
            showStatus("Game mode: " + G->world->meta.gameMode);
        }
        if (inScreen) break;
        if (k == SAPP_KEYCODE_F && !e->key_repeat) G->input.toggleFly = true;
        if (k == SAPP_KEYCODE_Q && !e->key_repeat) dropSelected((e->modifiers & SAPP_MODIFIER_CTRL) != 0);
        if (k >= SAPP_KEYCODE_1 && k <= SAPP_KEYCODE_9) G->hotbarSel = k - SAPP_KEYCODE_1;
        break;
    }
    case SAPP_EVENTTYPE_KEY_UP:
        if (e->key_code >= 0 && e->key_code < 512) G->keys[e->key_code] = false;
        break;
    case SAPP_EVENTTYPE_MOUSE_DOWN: {
        G->mouseX = e->mouse_x; G->mouseY = e->mouse_y;
        bool shift = (e->modifiers & SAPP_MODIFIER_SHIFT) != 0;
        if (inScreen) { screenMouseDown(e->mouse_button == SAPP_MOUSEBUTTON_RIGHT, shift); break; }
        if (!G->mouseLocked) { G->mouseLocked = true; sapp_lock_mouse(true); break; }
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) G->mouseDown[0] = true;
        else if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) { G->mouseDown[1] = true; const ItemDef* h = G->held(); if (!(h && h->isFood())) clickUse(); }
        else if (e->mouse_button == SAPP_MOUSEBUTTON_MIDDLE) pickBlock();
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) G->mouseDown[0] = false;
        if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) G->mouseDown[1] = false;
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        G->mouseX = e->mouse_x; G->mouseY = e->mouse_y;
        if (G->mouseLocked && !inScreen) {
            const float sens = 0.0025f;
            G->player.yaw += e->mouse_dx * sens;
            G->player.pitch = clamp(G->player.pitch - e->mouse_dy * sens, -1.55f, 1.55f);
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        if (inScreen) { screenScroll(e->scroll_y); break; }
        if (e->scroll_y != 0) G->hotbarSel = (G->hotbarSel + (e->scroll_y < 0 ? 1 : 8)) % 9;
        break;
    case SAPP_EVENTTYPE_UNFOCUSED:
        if (G->screen == Screen::None) G->screen = Screen::Pause;
        G->mouseLocked = false; sapp_lock_mouse(false);
        for (bool& k : G->keys) k = false;
        G->mouseDown[0] = G->mouseDown[1] = false;
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
        else if (a == "--creative") G->opt.creative = true;
        else if (a == "--radius") G->opt.radius = clamp(atoi(next()), 2, 32);
        else if (a == "--smoke") G->opt.smokeFrames = atoi(next());
        else if (a == "--screenshot") G->opt.screenshot = next();
    }
    logSetLevel(LogLevel::Info);
    fs::create_directories(userDataDir("Voxcraft"));
    logSetFile(userDataDir("Voxcraft") / "latest.log");
    stm_setup();
    sapp_desc d = {};
    d.init_cb = init; d.frame_cb = frame; d.cleanup_cb = cleanup; d.event_cb = event;
    d.width = 1280; d.height = 720; d.high_dpi = true; d.window_title = "Voxcraft";
    d.sample_count = 1; d.swap_interval = 1;
    d.logger.func = slog_func;
    return d;
}
