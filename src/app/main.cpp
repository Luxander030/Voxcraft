// Voxcraft entry point: sokol app callbacks, frame loop and input dispatch.
//
// Command line:
//   --world <name>   world folder under <user data>/Voxcraft/saves (default "World1")
//   --seed <n>       seed for a new world    --flat   flat preset    --creative   creative mode for a new world
//   --radius <n>     render distance in chunks
//   --smoke <frames> scripted run that prints stats and exits    --screenshot <png>  capture this window (macOS)
#include "game/building.h"
#include "app/game.h"
#include "core/profiler.h"
#include "sokol/sokol_log.h"
#include "sokol/sokol_time.h"
#include "sokol/sokol_audio.h"
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
    G->options.load(); applyOptions();
    if (G->opt.radius != 10) G->options.renderDistance = G->opt.radius;   // command line override
    if (G->assetDir.empty()) err = "assets not found (voxcraft.pak)";
    else if (!appInit(&err)) {}
    if (!err.empty()) { VX_ERROR("startup failed: %s", err.c_str()); G->initFailed = true; sapp_request_quit(); return; }
    fs::create_directories(userDataDir("Voxcraft") / "saves");
    G->view = G->opt.view;
    if (G->opt.directWorld) {   // --world / --smoke: skip the menus
        NewWorld nw; nw.seed = G->opt.seed; nw.seedSet = G->opt.seedSet; nw.flat = G->opt.flat; nw.creative = G->opt.creative;
        if (!startSession(G->opt.world, &nw, &err)) { VX_ERROR("world: %s", err.c_str()); sapp_request_quit(); return; }
        G->app = AppState::Playing;
    } else G->app = AppState::Title;
    G->lastSave = 0;
    if (G->noOcclusion) G->renderer.occlusionCulling = false;
}

void environment(FrameEnv& env, const Vec3& eye) {
    float ang = (float)(G->world->meta.dayTime - 6000) / 24000.0f * 2 * PI;
    float daylight = clamp(0.5f + std::cos(ang) * 0.9f, 0.12f, 1.0f);
    env.daylight = daylight;
    Vec3 day{0.62f, 0.78f, 1.0f}, night{0.02f, 0.03f, 0.07f};
    env.skyColor = night + (day - night) * ((daylight - 0.12f) / 0.88f);
    env.fogEnd = (float)(G->opt.radius * 16); env.fogStart = env.fogEnd * 0.6f;
    if (G->dimension == "overworld" && G->world->meta.raining) {   // overcast
        env.daylight *= 0.6f;
        Vec3 grey{0.45f, 0.48f, 0.52f};
        env.skyColor = grey * (0.2f + env.daylight);
        env.fogEnd *= 0.8f; env.fogStart *= 0.6f;
    }
    if (G->dimension == "hollow") {   // sealed caverns: no sky, warm haze
        env.daylight = 0; env.skyColor = {0.16f, 0.05f, 0.04f}; env.ambient = 0.32f;
        env.fogStart = 16; env.fogEnd = std::min(env.fogEnd, 96.0f);
    }
    BlockState headBlock = G->world->getBlock((int)std::floor(eye.x), (int)std::floor(eye.y), (int)std::floor(eye.z));
    if (G->reg.fluid(headBlock)) {
        bool lava = G->reg.def(headBlock).name == "lava";
        env.skyColor = lava ? Vec3{0.6f, 0.15f, 0.02f} : Vec3{0.05f, 0.15f, 0.4f} * std::max(0.3f, daylight);
        env.fogStart = 0; env.fogEnd = lava ? 2.0f : 24.0f;
    }
}

void frame() {
    if (G->initFailed) { sg_pass ps = {}; ps.swapchain = sglue_swapchain(); sg_begin_pass(&ps); sg_end_pass(); sg_commit(); return; }
    double dt = std::min(sapp_frame_duration(), 0.25);
    G->now += dt;
    if (G->quitToTitle) {   // deferred from the pause menu: never tear down a world mid-frame
        G->quitToTitle = false;
        if (endSession()) {
            refreshWorldList();
            G->app = AppState::Title; G->screen = Screen::Pause; G->mouseLocked = false; sapp_lock_mouse(false);
        } else showSaveFailure();
    }
    if (G->app != AppState::Playing || !G->world) {
        drawMenus(sapp_width(), sapp_height());
        G->jobs->pumpMain(2.0);   // drain stale results from a closed world
        FrameEnv env; env.skyColor = {0.05f, 0.05f, 0.07f};
        G->renderer.render({0, 0, 0}, 0, 0, sapp_width(), sapp_height(), env, nullptr, G->ui, nullptr);
        if (G->opt.smokeFrames) {   // --title-smoke: capture the menu and exit
            G->frame++;
            if (G->frame == 2 && !G->opt.screenshot.empty()) vx_platform_activate();   // only steal focus when capturing
            if (G->opt.menuState == 1 && G->frame == 5) { refreshWorldList(); G->app = AppState::Worlds; }
            if (G->opt.menuState == 2 && G->frame == 5) { G->app = AppState::CreateWorld; G->focusField = 1; }
            if (G->opt.menuState == 3 && G->frame == 5) { G->app = AppState::Settings; }
            if ((int)G->frame == G->opt.smokeFrames - 5 && !G->opt.screenshot.empty()) {
#if defined(__APPLE__)
                if (int id = vx_platform_window_id()) { std::string cmd = "screencapture -x -o -l" + std::to_string(id) + " '" + G->opt.screenshot + "'"; if (std::system(cmd.c_str()) != 0) VX_WARN("screenshot failed"); }
#endif
            }
            if ((int)G->frame >= G->opt.smokeFrames) { printf("title smoke: %llu frames ok\n", (unsigned long long)G->frame); sapp_request_quit(); }
        }
        return;
    }
    float ms = (float)(dt * 1000.0);
    G->frameMsAvg = G->frameMsAvg * 0.95f + ms * 0.05f; G->fps = 1000.0f / std::max(0.1f, G->frameMsAvg);
    G->frameMsMax = std::max(G->frameMsMax * 0.995f, ms);
    G->frame++;

    if (G->opt.smokeFrames) {   // scripted: look around, fly forward, then pose creatures and open the inventory
        int last = G->opt.smokeFrames;
        if (G->frame == 2 && !G->opt.screenshot.empty()) vx_platform_activate();   // only steal focus when capturing
        // focus changes while the window activates would pause the game; the script stays in control
        if (G->screen == Screen::Pause && (int)G->frame < last - 30) G->screen = Screen::None;
        if (G->opt.menuState == 4 && (int)G->frame == last - 30) { G->screen = Screen::Pause; G->mouseLocked = false; }
        if ((G->opt.menuState == 6 || G->opt.menuState == 7 || G->opt.menuState == 8) && G->frame == 5) {
            G->world->meta.raining = G->opt.menuState == 6;
            G->world->meta.dayTime = G->opt.menuState == 7 ? 18000 : G->opt.menuState == 8 ? 4500 : 6000;
        }
        if (G->opt.menuState >= 6 && G->opt.menuState != 12 && (int)G->frame >= last - 20) { G->player.pitch = G->opt.menuState == 7 ? 0.9f : G->opt.menuState == 8 ? 1.2f : 0.3f; if (G->opt.menuState == 8) G->player.yaw = PI * 0.5f; }
        if (G->smokeUnderground && G->frame == 20 && G->playerReady) {   // measurement: a pocket deep underground
            BlockPos c{(int)std::floor(G->player.pos.x), 24, (int)std::floor(G->player.pos.z)};
            for (int dy = 0; dy < 3; dy++) for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) G->world->setBlock({c.x + dx, c.y + dy, c.z + dz}, AIR);
            G->player.pos = {c.x + 0.5f, (float)c.y, c.z + 0.5f}; G->player.prevPos = G->player.pos; G->player.flying = false;
        }
        if (G->smokeUnderground && G->frame > 20) { G->input.forward = 0; }
        if (G->opt.menuState == 10 && G->frame == 10) {   // armor + farming kit, then the inventory
            G->armor[1] = {G->items.id("iron_chestplate"), 1, 40}; G->armor[3] = {G->items.id("hide_boots"), 1, 0}; G->armor[0] = {G->items.id("azurite_helm"), 1, 0};
            for (const char* n : {"iron_hoe", "grain_seeds", "bread", "bow", "arrow", "bedroll", "hopper", "comparator", "ember_striker"}) giveOrDropInv({G->items.id(n), (u8)(G->items.maxStack(G->items.id(n)) > 1 ? 12 : 1), 0});
        }
        if (G->opt.menuState == 10 && (int)G->frame == last - 30) openScreen(Screen::Inventory);
        if (G->opt.menuState == 11 && G->frame >= 20) {   // vista: high viewpoint over the landscape
            if (G->frame == 20) { G->player.pos.y = 140; G->player.prevPos = G->player.pos; }
            G->player.flying = true; G->player.vel = {}; G->player.pitch = -0.18f; G->input.forward = 0;
        }
        static bool built = false;
        if (G->opt.menuState == 12 && G->frame >= 30 && G->playerReady && !built) {
            built = true;   // building shapes showcase on a flat pad
            Vec3 l = G->player.look(); int f = std::fabs(l.x) > std::fabs(l.z) ? (l.x > 0 ? 3 : 2) : (l.z > 0 ? 1 : 0);
            int fx, fz; dirOffset(f, fx, fz); int rx = -fz, rz = fx;   // forward and right
            BlockPos o{(int)std::floor(G->player.pos.x), (int)std::floor(G->player.pos.y), (int)std::floor(G->player.pos.z)};
            auto at = [&](int fwd, int right, int up) { return BlockPos{o.x + fx * fwd + rx * right, o.y + up, o.z + fz * fwd + rz * right}; };
            for (int a = 2; a <= 9; a++) for (int b = -6; b <= 6; b++) { G->world->setBlock(at(a, b, -1), G->reg.stateOf("stone_bricks")); for (int u = 0; u < 4; u++) G->world->setBlock(at(a, b, u), AIR); }
            auto put = [&](BlockPos p, const char* n, std::initializer_list<std::pair<const char*, int>> props) {
                BlockState s = G->reg.stateOf(n); for (auto& [k, v] : props) s = G->reg.withProp(s, k, v);
                G->world->setBlock(p, s);
            };
            int back = f, toward = dirOpposite(f);   // stairs rise away from the camera; doors face it
            for (int b = -6; b <= -3; b++) put(at(5, b, 0), "oak_stairs", {{"facing", back}});
            for (int b = -6; b <= -3; b++) put(at(6, b, 1), "oak_slab", {{"type", 0}});
            put(at(4, -2, 0), "cobblestone_slab", {{"type", 0}}); put(at(4, -1, 0), "cobblestone_slab", {{"type", 2}}); put(at(4, 0, 0), "brick_stairs", {{"facing", back}, {"half", 1}});
            for (int b = 1; b <= 6; b++) put(at(7, b, 0), "oak_fence", {});
            put(at(6, 6, 0), "oak_fence", {});
            for (int b = 1; b <= 4; b++) put(at(5, b, 0), "glass_pane", {});
            put(at(5, 5, 0), "glass", {});
            put(at(3, 3, 0), "oak_trapdoor", {{"facing", f}}); put(at(3, 4, 0), "oak_trapdoor", {{"facing", toward}, {"open", 1}});
            for (int k = 0; k < 2; k++) {   // a pair of doors in a short wall, one open
                PlacePlan pl; pl.ok = true; pl.at = at(8, k, 0);
                pl.state = G->reg.withProp(G->reg.withProp(G->reg.withProp(G->reg.stateOf(k ? "iron_door" : "oak_door"), "facing", toward), "hinge", k), "open", k == 0);
                pl.upper = G->reg.withProp(pl.state, "upper", 1);
                applyPlacement(*G->world, pl);
            }
            for (int b = -2; b <= 3; b++) if (b < 0 || b > 1) for (int u = 0; u < 3; u++) G->world->setBlock(at(8, b, u), G->reg.stateOf("oak_planks"));
            for (int b = -2; b <= 3; b++) G->world->setBlock(at(8, b, 2), G->reg.stateOf("oak_planks"));
            for (int b = 1; b <= 6; b++) put(at(7, b, 0), "oak_fence", {});   // connections after neighbours exist
            G->player.pitch = -0.35f;
        }
        if (G->opt.menuState == 12 && G->frame > 30) { G->input.forward = 0; G->player.flying = false; G->player.pitch = -0.35f; }
        if (G->opt.menuState == 9 && G->frame == 20) {   // visit the nearest village
            int vx, vy, vz;
            if (G->world->generator().nearestVillage((int)G->player.pos.x, (int)G->player.pos.z, 8, vx, vy, vz)) {
                G->player.pos = {vx + 18.5f, (float)vy + 12, vz + 18.5f}; G->player.prevPos = G->player.pos; G->playerReady = false;
                G->player.yaw = std::atan2((float)vx - G->player.pos.x, -((float)vz - G->player.pos.z)); G->player.flying = true;
                fprintf(stderr, "village at %d %d %d\n", vx, vy, vz);
            } else fprintf(stderr, "no village found\n");
        }
        if (G->opt.menuState == 9 && (int)G->frame > 20) { G->player.pitch = -0.45f; G->input.forward = 0; G->player.flying = true; G->player.vel = {}; }
        if (G->opt.menuState == 5 && (int)G->frame == 30 && G->playerReady) {   // travel to the Hollow through a gate
            std::string err;
            Vec3 t{std::floor(G->player.pos.x / 8), 64, std::floor(G->player.pos.z / 8)};
            if (!enterDimension(G->dimension == "hollow" ? "overworld" : "hollow", t, true, &err)) fprintf(stderr, "travel failed: %s\n", err.c_str());
        }
        bool posing = (int)G->frame > last - 90;
        if (G->opt.menuState == 9 || G->opt.menuState == 11 || G->opt.menuState == 12) posing = true;
        if (!posing) { G->player.yaw += 0.01f; G->input.forward = G->frame > 120 && G->frame < 400 ? 1.0f : 0.0f; }
        else G->input.forward = 0;
        G->player.flying = G->creative() && !posing;
        if ((int)G->frame == last - 90 && G->playerReady && G->opt.menuState != 9 && G->opt.menuState != 12) {   // one of each creature in front of the camera
            Vec3 l = G->player.look(); Vec3 f = normalize(Vec3(l.x, 0, l.z)), r{-f.z, 0, f.x};
            G->player.pitch = -0.25f;
            for (u16 t = 0; t < G->mobReg.size(); t++) {
                Vec3 at = G->player.pos + f * 5.0f + r * ((t - 1.5f) * 1.6f);
                at.y = (float)(G->world->surfaceY((int)std::floor(at.x), (int)std::floor(at.z)) + 1);
                G->mobs->spawn(t, at, G->rng).yaw = G->player.yaw + PI;
            }
        }
        if ((int)G->frame == last - 88 && G->playerReady) {   // a torch to show flames
            Vec3 l = G->player.look(); Vec3 f = normalize(Vec3(l.x, 0, l.z));
            Vec3 at = G->player.pos + f * 3.0f;
            BlockPos tp{(int)std::floor(at.x), G->world->surfaceY((int)std::floor(at.x), (int)std::floor(at.z)) + 1, (int)std::floor(at.z)};
            G->world->setBlock(tp, G->reg.withProp(G->reg.stateOf("torch"), "facing", 4));
        }
        if ((int)G->frame >= last - 14 && (int)G->frame <= last - 8 && G->playerReady) {   // debris burst
            Vec3 l = G->player.look(); Vec3 f = normalize(Vec3(l.x, 0, l.z));
            Vec3 at = G->player.pos + f * 2.5f;
            BlockPos b{(int)std::floor(at.x), (int)std::floor(G->player.pos.y), (int)std::floor(at.z)};
            G->particles.blockDebris(b, G->renderer.textures().face(G->reg.stateOf("bricks"), FACE_NZ), 0xFFFFFFFFu, 12, G->rng);
        }
        if (!G->opt.screenshot.empty() && G->opt.screenshot.find("inv") != std::string::npos && (int)G->frame == last - 30) openScreen(Screen::Inventory);
    }

    Player& p = G->player;
    ChunkPos pc{(int)std::floor(p.pos.x) >> 4, (int)std::floor(p.pos.z) >> 4};
    if (!G->playerReady && G->world->isReady(pc)) {
        while (p.pos.y < WORLD_H - 2 && G->reg.solid(G->world->getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)))) p.pos.y += 1;
        p.prevPos = p.pos;
        G->playerReady = true;
    }
    bool simulate = G->playerReady && G->world->isReady(pc) && G->screen != Screen::Pause && G->screen != Screen::Settings && G->screen != Screen::Controls;
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

    if (G->mixer) G->mixer->setListener(eye, p.yaw);
    if (G->now >= G->nextBudgetCheck) {   // memory budgets: adapt the render distance under pressure
        G->nextBudgetCheck = G->now + 1.0;
        BudgetInputs bi;
        bi.meshBytes = G->renderer.stats().gpuBytes; bi.processBytes = (size_t)processMemoryBytes();
        u64 phys = physicalMemoryBytes();
        bi.processBudget = phys ? (size_t)std::min<u64>(phys / 2, 4096ull << 20) : (2048ull << 20);
        bi.desiredRadius = G->opt.radius;
        if (G->budget.update(bi)) VX_INFO("memory budget: render distance now %d (mesh %.0f MiB, rss %.0f MiB)", G->budget.effectiveRadius, bi.meshBytes / 1048576.0, bi.processBytes / 1048576.0);
        G->world->budget.chunkCacheBytes = G->budget.chunkCacheBytes;
    }
    int radius = std::min(G->opt.radius, G->budget.effectiveRadius);
    // distant terrain LOD: rebuilt on a worker (with its own generator instance) after 64 blocks of movement
    // (flat worlds have nothing to show in the distance, and column() describes default terrain)
    G->renderer.farTerrainEnabled = G->options.distantTerrain && G->dimension == "overworld" && G->world->meta.gen.preset != "flat";
    if (G->renderer.farTerrainEnabled && !G->farInFlight) {
        int cx = (int)std::floor(renderPos.x / 8) * 8, cz = (int)std::floor(renderPos.z / 8) * 8;
        if (std::abs(cx - G->farCX) > 64 || std::abs(cz - G->farCZ) > 64 || G->farRadiusBuilt != radius) {
            G->farInFlight = true; G->farCX = cx; G->farCZ = cz; G->farRadiusBuilt = radius;
            GenSettings gs = G->world->meta.gen; u32 epoch = G->farEpoch;
            int inner = radius * 16, outer = std::min(radius * 16 * 3, 512);
            G->jobs->submit([gs, cx, cz, inner, outer, epoch] {
                auto gen = createGenerator(gs, G->reg);
                auto mesh = std::make_shared<FarTerrainMesh>();
                if (gen) buildFarTerrain(*gen, cx, cz, inner, outer, 8, *mesh);
                G->jobs->postMain([mesh, epoch, outer] {
                    G->farInFlight = false;
                    if (epoch != G->farEpoch || !G->world) return;   // a different world/dimension by now
                    G->renderer.setFarTerrain(*mesh); G->renderer.farRadius = (float)outer;
                });
            });
        }
    }
    G->world->setFocus(renderPos, radius + 2);
    G->world->update(4.0);
    G->renderer.updateMeshes(eye, radius);
    if (G->now - G->lastSave > 45.0) { saveWorld(false); G->lastSave = G->now; }

    FrameEnv env; environment(env, eye);
    env.fovY = G->options.fov * PI / 180.0f;
    env.fogEnd = std::min(env.fogEnd, (float)(std::min(G->opt.radius, G->budget.effectiveRadius) * 16));
    env.fogStart = std::min(env.fogStart, env.fogEnd * 0.6f);
    if (G->renderer.farTerrainEnabled && G->renderer.farRadius > 0 && G->dimension == "overworld" && G->reg.fluid(G->world->getBlock((int)std::floor(eye.x), (int)std::floor(eye.y), (int)std::floor(eye.z))) == false) {
        env.fogStart = env.fogEnd * 0.9f; env.fogEnd = G->renderer.farRadius * 0.97f;   // the horizon moves out to the LOD ring
    }
    drawFrameUi(sapp_width(), sapp_height());
    // Third-person camera: pulled back along the view ray (or ahead of the player, looking back),
    // stopping short of blocks so the view never ends up inside terrain. Targeting and audio stay
    // at the player's eye.
    Vec3 camEye = eye; float camYaw = p.yaw, camPitch = p.pitch;
    if (G->view != 0 && !G->stats.dead) {
        Vec3 dir = G->view == 1 ? p.look() * -1.0f : p.look();
        if (G->view == 2) { camYaw = p.yaw + PI; camPitch = -p.pitch; }
        float want = 4.0f, dist = want;
        // probe a few rays around the centre so the near plane does not clip walls at the edges
        const Vec3 off[5] = {{0, 0, 0}, {0.15f, 0.15f, 0.1f}, {-0.15f, 0.15f, -0.1f}, {0.15f, -0.15f, -0.1f}, {-0.15f, -0.15f, 0.1f}};
        for (const Vec3& o : off) {   // only solid blocks stop the camera (not grass, flowers, torches)
            for (float t = 0; t < dist; t += 0.05f) {
                Vec3 q = eye + o + dir * t;
                if (G->reg.solid(G->world->getBlock((int)std::floor(q.x), (int)std::floor(q.y), (int)std::floor(q.z)))) { dist = std::max(0.0f, t - 0.25f); break; }
            }
        }
        camEye = eye + dir * dist;
    }
    G->camYaw = camYaw; G->camPitch = camPitch;
    buildEntityMesh(G->entityVerts, camEye, alpha, (float)dt);
    if (G->view != 0) buildPlayerMesh(G->entityVerts, camEye, renderPos, alpha, (float)dt);
    else G->swingRequested = false;
    if (G->target.hit) G->renderer.outlineBox = blockShapeBounds(G->reg, G->world->getBlock(G->target.block));
    G->renderer.render(camEye, camYaw, camPitch, sapp_width(), sapp_height(), env, G->screen == Screen::None ? &G->target : nullptr, G->ui, &G->entityVerts);

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
        int settlers = 0; for (auto& m : G->mobs->mobs) settlers += G->mobReg.def(m.type).trader;
        printf("smoke [%s] mobs %zu (settlers %d) villages %zu: %llu frames, avg %.2f ms, chunks ready %d, sections %d visible %d, draws %d, tris %llu, mesh VRAM %.1f MiB, rss %.0f MiB\n",
               G->dimension.c_str(), G->mobs->mobs.size(), settlers, G->world->meta.populatedVillages.size(), (unsigned long long)G->frame, G->frameMsAvg, ws.ready, rs.sections, rs.visibleSections, rs.drawCalls, (unsigned long long)(rs.quads * 2),
               rs.gpuBytes / 1048576.0, processMemoryBytes() / 1048576.0);
        sapp_request_quit();
    }
}

void cleanup() {
    endSession(true);
    saudio_shutdown();
    G->mixer.reset();
    sg_shutdown();
    G->jobs.reset();
    delete G; G = nullptr;
}

void event(const sapp_event* e) {
    if (G->initFailed) return;
    if (e->type == SAPP_EVENTTYPE_QUIT_REQUESTED) {
        // Closing the window: save now, while the quit can still be cancelled. If the save fails,
        // keep running so nothing unsaved is lost; the player can free space and try again.
        if (G->world && !saveWorld(true)) { sapp_cancel_quit(); showSaveFailure(); }
        return;
    }
    // ---- menus (title, worlds, create, settings) and in-game menu overlays
    bool overlay = G->app == AppState::Playing && (G->screen == Screen::Pause || G->screen == Screen::Settings || G->screen == Screen::Controls);
    if (G->app != AppState::Playing || overlay || (G->app == AppState::Playing && G->screen == Screen::Chat)) {
        switch (e->type) {
        case SAPP_EVENTTYPE_MOUSE_MOVE: G->mouseX = e->mouse_x; G->mouseY = e->mouse_y; return;
        case SAPP_EVENTTYPE_MOUSE_DOWN: G->mouseX = e->mouse_x; G->mouseY = e->mouse_y; if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) { G->clicked = true; G->mouseHeld = true; } return;
        case SAPP_EVENTTYPE_MOUSE_UP: if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) G->mouseHeld = false; return;
        case SAPP_EVENTTYPE_MOUSE_SCROLL: if (G->app == AppState::Worlds) G->worldScroll += e->scroll_y < 0 ? 1 : -1; return;
        case SAPP_EVENTTYPE_CHAR: if (G->ignoreChar) { G->ignoreChar = false; return; } menuChar(e->char_code); return;
        case SAPP_EVENTTYPE_KEY_DOWN:
            menuKey(e->key_code);
            if (e->key_code == SAPP_KEYCODE_ESCAPE) {
                if (G->rebinding >= 0) { /* handled by menuKey: cancels the rebind */ }
                else if (G->app == AppState::Playing) { if (G->screen == Screen::Settings) { G->options.save(); applyOptions(); G->screen = Screen::Pause; } else if (G->screen == Screen::Controls) { G->options.save(); G->screen = Screen::Settings; } else closeScreen(); }
                else if (G->app == AppState::Controls) { G->options.save(); G->app = AppState::Settings; }
                else if (G->app == AppState::Worlds || G->app == AppState::Settings) { G->options.save(); applyOptions(); G->app = AppState::Title; }
                else if (G->app == AppState::CreateWorld || G->app == AppState::ConfirmDelete) G->app = AppState::Worlds;
            }
            return;
        case SAPP_EVENTTYPE_QUIT_REQUESTED: return;
        default: if (!overlay && G->app != AppState::Playing) return; break;
        }
        if (G->app != AppState::Playing) return;
    }
    if (!G->world) return;
    bool inScreen = G->screen != Screen::None;
    switch (e->type) {
    case SAPP_EVENTTYPE_KEY_DOWN: {
        if (e->key_code >= 0 && e->key_code < 512 && !inScreen) G->keys[e->key_code] = true;
        sapp_keycode k = e->key_code;
        if (k == SAPP_KEYCODE_ESCAPE) {
            if (G->screen == Screen::None) { G->screen = Screen::Pause; G->mouseLocked = false; sapp_lock_mouse(false); for (bool& kk : G->keys) kk = false; }
            else if (G->screen != Screen::Dead) closeScreen();
        }
        if (((int)k == G->options.keys[UserOptions::Chat] || k == SAPP_KEYCODE_SLASH) && G->screen == Screen::None && !e->key_repeat) {
            openScreen(Screen::Chat); G->chatLine = k == SAPP_KEYCODE_SLASH ? "/" : ""; G->ignoreChar = true; break;
        }
        if (G->screen == Screen::None && !e->key_repeat) {
            if (G->options.toggleSneak && (int)k == G->options.keys[UserOptions::Sneak]) G->sneakLatched = !G->sneakLatched;
            if (G->options.toggleSprint && (int)k == G->options.keys[UserOptions::Sprint]) G->sprintLatched = !G->sprintLatched;
        }
        if ((int)k == G->options.keys[UserOptions::InventoryKey] && !e->key_repeat) {
            if (G->screen == Screen::None) openScreen(Screen::Inventory);
            else if (G->screen != Screen::Pause && G->screen != Screen::Dead) closeScreen();
        }
        if (k == SAPP_KEYCODE_F3) G->debug = !G->debug;
        if (k == SAPP_KEYCODE_F11) sapp_toggle_fullscreen();
        if (k == SAPP_KEYCODE_F6 && !e->key_repeat) { G->renderer.occlusionCulling = !G->renderer.occlusionCulling; showStatus(G->renderer.occlusionCulling ? "Occlusion culling on" : "Occlusion culling off"); }
        if (k == SAPP_KEYCODE_F5 && !e->key_repeat) { if (saveWorld(true)) showStatus("World saved"); else showSaveFailure(); }
        if (k == SAPP_KEYCODE_F4 && !e->key_repeat) {   // developer convenience: switch game mode
            G->world->meta.gameMode = G->creative() ? "survival" : "creative";
            G->player.canFly = G->creative(); if (!G->creative()) G->player.flying = false;
            showStatus("Game mode: " + G->world->meta.gameMode);
        }
        if (inScreen) break;
        if ((int)k == G->options.keys[UserOptions::View] && !e->key_repeat) G->view = (G->view + 1) % 3;
        if (k == SAPP_KEYCODE_F && !e->key_repeat) G->input.toggleFly = true;
        if ((int)k == G->options.keys[UserOptions::Drop] && !e->key_repeat) dropSelected((e->modifiers & SAPP_MODIFIER_CTRL) != 0);
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
        if (e->mouse_button == SAPP_MOUSEBUTTON_LEFT) { if (!attackMob()) G->mouseDown[0] = true; }
        else if (e->mouse_button == SAPP_MOUSEBUTTON_RIGHT) { G->mouseDown[1] = true; const ItemDef* h = G->held(); if (!(h && (h->isFood() || h->name == "bow"))) clickUse(); }
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
            const float sens = 0.0025f * G->options.sensitivity;
            G->player.yaw += e->mouse_dx * sens;
            G->player.pitch = clamp(G->player.pitch - e->mouse_dy * sens * (G->options.invertMouse ? -1.0f : 1.0f), -1.55f, 1.55f);
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
        if (a == "--world") { G->opt.world = next(); G->opt.directWorld = true; }
        else if (a == "--seed") { G->opt.seed = std::strtoull(next(), nullptr, 10); G->opt.seedSet = true; }
        else if (a == "--flat") G->opt.flat = true;
        else if (a == "--creative") G->opt.creative = true;
        else if (a == "--radius") G->opt.radius = clamp(atoi(next()), 2, 32);
        else if (a == "--smoke") { G->opt.smokeFrames = atoi(next()); G->opt.directWorld = true; }
        else if (a == "--screenshot") G->opt.screenshot = next();
        else if (a == "--view") G->opt.view = atoi(next()) % 3;
        else if (a == "--no-occlusion") G->noOcclusion = true;
        else if (a == "--underground") G->smokeUnderground = true;
        else if (a == "--title-smoke") { G->opt.smokeFrames = atoi(next()); G->opt.directWorld = false; }
        else if (a == "--menu") { std::string m = next(); G->opt.menuState = m == "worlds" ? 1 : m == "create" ? 2 : m == "settings" ? 3 : m == "pause" ? 4 : m == "hollow" ? 5 : m == "rain" ? 6 : m == "night" ? 7 : m == "sky" ? 8 : m == "village" ? 9 : m == "armor" ? 10 : m == "vista" ? 11 : m == "build" ? 12 : 0; }
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
