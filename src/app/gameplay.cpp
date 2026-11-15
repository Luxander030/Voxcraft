// Session lifecycle, persistence of player state, simulation tick and block interaction.
#include "app/game.h"
#include "core/profiler.h"
#include "sokol/sokol_audio.h"
#include "sokol/sokol_log.h"
#include <ctime>

namespace vx {

Game* G = nullptr;

namespace {
struct PackNames final : TexturePackNames {
    const TexturePack& p; explicit PackNames(const TexturePack& pk) : p(pk) {}
    int layerOf(const std::string& n) const override { return p.layer(n); }
};

fs::path dataFile(const char* name) {
    fs::path a = G->assetDir / name;
    if (fs::exists(a)) return a;
    return fs::path(VX_SOURCE_DIR) / "assets" / "data" / name;
}

std::vector<u8> encodePlayer() {
    ByteWriter w; w.u8_(2);
    const Player& p = G->player;
    w.f64_(p.pos.x); w.f64_(p.pos.y); w.f64_(p.pos.z); w.f32_(p.yaw); w.f32_(p.pitch); w.u8_(p.flying ? 1 : 0); w.u8_((u8)G->hotbarSel);
    auto st = G->stats.encode(); w.var(st.size()); w.bytes(st.data(), st.size());
    w.var(G->inv.slots.size());
    for (auto& s : G->inv.slots) { w.str(s.empty() ? "" : G->items.def(s.item).name); w.u8_(s.empty() ? 0 : s.count); w.var(s.damage); }
    return std::move(w.buf);
}

bool decodePlayer(const std::vector<u8>& b) {
    ByteReader r(b); u8 ver = r.u8_();
    if (ver != 1 && ver != 2) return false;
    Player& p = G->player;
    p.pos.x = (float)r.f64_(); p.pos.y = (float)r.f64_(); p.pos.z = (float)r.f64_(); p.yaw = r.f32_(); p.pitch = r.f32_();
    p.flying = r.u8_() != 0; G->hotbarSel = clamp((int)r.u8_(), 0, 8);
    if (ver >= 2) {
        u64 n = r.var(); const u8* sp = r.skip((size_t)n);
        if (sp) { ByteReader sr(sp, (size_t)n); G->stats.decode(sr); }
        u64 slots = r.var();
        for (u64 i = 0; i < slots && r.ok(); i++) {
            std::string name = r.str(); u8 c = r.u8_(); u16 dmg = (u16)r.var();
            if (i < G->inv.slots.size()) {
                u16 id = G->items.id(name);
                if (!name.empty() && !id) VX_WARN("player inventory: unknown item '%s' dropped", name.c_str());
                G->inv.slots[i] = id ? ItemStack{id, c, dmg} : ItemStack{};
            }
        }
    }
    return r.ok();
}

void giveCreativeHotbar() {
    const char* names[9] = {"stone", "cobblestone", "oak_planks", "glass", "bricks", "oak_log", "torch", "glowcrystal", "chest"};
    for (int i = 0; i < 9; i++) if (G->inv.slots[i].empty()) G->inv.slots[i] = {G->items.id(names[i]), 64, 0};
}

Vec3 surfaceSpawn() {
    const BlockPos& s = G->world->meta.spawn;
    return {s.x + 0.5f, (float)s.y, s.z + 0.5f};
}

void spill(const BlockPos& p, std::vector<ItemStack>& stacks) {
    for (auto& s : stacks) if (!s.empty()) { G->entities->spawnItem({p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}, s, G->rng); s = {}; }
}

void breakAt(const BlockPos& pos, bool withDrops) {
    BlockState s = G->world->getBlock(pos);
    if (s == AIR) return;
    const BlockDef& d = G->reg.def(s);
    if (BlockEntity* be = G->world->blockEntity(pos)) spill(pos, be->items);
    G->world->setBlock(pos, AIR);
    sfx3d("break_" + d.sound, {pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f}, SoundCat::Blocks);
    if (withDrops) {
        auto drops = blockDrops(s, G->reg, G->items, G->recipes, G->held(), G->rng);
        for (auto& it : drops) G->entities->spawnItem({pos.x + 0.5f, pos.y + 0.3f, pos.z + 0.5f}, it, G->rng);
    }
    // things that cannot stand on their own fall with their support
    BlockPos up{pos.x, pos.y + 1, pos.z};
    BlockState a = G->world->getBlock(up);
    const BlockDef& ad = G->reg.def(a);
    if (a != AIR && (ad.has(BF_NEEDS_SOIL) || (ad.has(BF_ATTACHED) && ad.propIndex("facing") >= 0 && G->reg.getProp(a, "facing") == 4) || ad.has(BF_ATTACHED_FLOOR)))
        breakAt(up, withDrops && !G->creative());
    (void)d;
}

void damageHeldTool() {
    if (G->creative()) return;
    ItemStack& s = G->inv.slots[G->hotbarSel];
    if (s.empty()) return;
    const ItemDef& d = G->items.def(s.item);
    if (d.durability <= 0) return;
    if (++s.damage >= d.durability) { s = {}; showStatus(d.name + " broke"); }
}

int horizontalFacingToward(const Player& p) {   // front of the block faces the player
    Vec3 l = p.look();
    if (std::fabs(l.x) > std::fabs(l.z)) return l.x > 0 ? 2 : 3;
    return l.z > 0 ? 0 : 1;
}

bool placeHeld() {
    if (!G->target.hit) return false;
    ItemStack& hs = G->inv.slots[G->hotbarSel];
    if (hs.empty()) return false;
    BlockState place = G->items.def(hs.item).places;
    if (place == AIR) return false;
    BlockPos at = G->target.block;
    if (!G->reg.def(G->world->getBlock(at)).has(BF_REPLACEABLE)) at = at + G->target.normal;
    if (!G->reg.def(G->world->getBlock(at)).has(BF_REPLACEABLE)) return false;
    const BlockDef& d = G->reg.def(place);
    if (d.solid) {
        AABB bb{{(float)at.x, (float)at.y, (float)at.z}, {(float)at.x + 1, (float)at.y + 1, (float)at.z + 1}};
        if (bb.intersects(G->player.box())) return false;
    }
    const BlockPos& n = G->target.normal;
    BlockState below = G->world->getBlock({at.x, at.y - 1, at.z});
    if (d.has(BF_NEEDS_SOIL)) {
        const std::string& bn = G->reg.def(below).name;
        if (bn != "turf" && bn != "dirt" && bn != "podzol" && !(d.name == "cactus" && (bn == "sand" || bn == "cactus")) && !(d.name == "dead_bush" && bn == "sand")) return false;
    }
    if (d.has(BF_ATTACHED_FLOOR) && !G->reg.solid(below)) return false;
    if (d.propIndex("facing") >= 0) {
        int cnt = d.props[d.propIndex("facing")].count;
        if (d.shape == BlockShape::Torch) {
            if (n.y < 0) return false;
            int f = n.y > 0 ? 4 : n.z < 0 ? 0 : n.z > 0 ? 1 : n.x < 0 ? 2 : 3;
            if (f == 4 && !G->reg.solid(below)) return false;
            place = G->reg.withProp(place, "facing", f);
        } else if (d.shape == BlockShape::FlatWall) {
            if (n.y != 0) return false;
            place = G->reg.withProp(place, "facing", n.z < 0 ? 0 : n.z > 0 ? 1 : n.x < 0 ? 2 : 3);
        } else if (cnt == 4) {
            place = G->reg.withProp(place, "facing", horizontalFacingToward(G->player));
        } else if (cnt == 6) {
            Vec3 l = G->player.look(); int f;
            if (std::fabs(l.y) > 0.7f) f = l.y > 0 ? 0 : 1; else f = horizontalFacingToward(G->player) + 2;
            place = G->reg.withProp(place, "facing", f);
        }
    }
    if (!G->world->setBlock(at, place)) return false;
    sfx3d("place_" + d.sound, {at.x + 0.5f, at.y + 0.5f, at.z + 0.5f}, SoundCat::Blocks);
    if (d.has(BF_INTERACT_CHEST)) G->world->ensureBlockEntity(at, BE_CHEST)->items.resize(CHEST_SLOTS);
    if (d.has(BF_INTERACT_KILN)) G->world->ensureBlockEntity(at, BE_KILN)->items.resize(KILN_SLOTS);
    if (!G->creative()) { hs.count--; if (!hs.count) hs = {}; }
    return true;
}

void tickKilns() {
    // Only kilns in loaded chunks near the player run (simulation distance).
    int r = G->world->simulationRadius;
    ChunkPos pc{(int)std::floor(G->player.pos.x) >> 4, (int)std::floor(G->player.pos.z) >> 4};
    std::vector<std::pair<BlockPos, bool>> litChanges;
    G->world->forEachChunk([&](ChunkPos p, Chunk& c, ChunkState) {
        if (std::abs(p.x - pc.x) > r || std::abs(p.z - pc.z) > r) return;
        for (auto& be : c.blockEntities) {
            if (be.kind != BE_KILN) continue;
            bool lit = false;
            if (tickKiln(be, G->items, G->recipes, lit)) c.unsaved = true;
            int lx, y, lz; columnIndexDecode(be.index, lx, y, lz);
            BlockState s = c.get(lx, y, lz);
            if (G->reg.def(s).propIndex("lit") >= 0 && (G->reg.getProp(s, "lit") != 0) != lit) litChanges.push_back({{p.x * 16 + lx, y, p.z * 16 + lz}, lit});
        }
    });
    for (auto& [bp, lit] : litChanges) G->world->setBlock(bp, G->reg.withProp(G->world->getBlock(bp), "lit", lit ? 1 : 0));
}
} // namespace

void giveOrDropInv(ItemStack s) {
    int left = G->inv.add(s, G->items);
    if (left) { s.count = (u8)left; G->entities->spawnItem(G->player.eye(), s, G->rng, G->player.look() * 0.3f); }
}

void showStatus(const std::string& s) { G->status = s; G->statusUntil = G->now + 3; }

bool appInit(std::string* err) {
    if (!G->reg.loadFile(dataFile("blocks.json").string(), err)) return false;
    if (!G->pack.load((G->assetDir / "voxcraft.pak").string(), err)) return false;
    PackNames pn(G->pack);
    if (!G->items.loadFile(dataFile("items.json").string(), G->reg, &pn, err)) return false;
    if (!G->recipes.loadFile(dataFile("recipes.json").string(), G->items, err)) return false;
    {
        auto readJson = [&](const fs::path& p, Json& out) {
            auto t = readTextFile(p); if (!t) { if (err) *err = "cannot read " + p.string(); return false; }
            std::string e; out = Json::parse(*t, &e); if (!e.empty()) { if (err) *err = p.string() + ": " + e; return false; } return true;
        };
        Json mobsJ, modelsJ, animJ;
        fs::path models = G->assetDir / "models.json";
        if (!readJson(dataFile("mobs.json"), mobsJ) || !readJson(models, modelsJ) || !readJson(dataFile("animations.json"), animJ)) return false;
        if (!G->mobReg.load(mobsJ, modelsJ, animJ, G->items, &pn, err)) return false;
    }
    G->jobs = std::make_unique<JobSystem>(0);
    if (!G->renderer.init(G->reg, G->pack, *G->jobs, err)) return false;
    {   // audio: synthesize the bank (~0.1 s), then start the output stream
        Stopwatch sw; G->sounds.generate();
        G->mixer = std::make_unique<Mixer>(G->sounds);
        VX_INFO("audio: %zu sounds, %.1f KiB, %.0f ms", G->sounds.count(), G->sounds.bytes() / 1024.0, sw.ms());
        saudio_desc ad = {}; ad.sample_rate = Mixer::RATE; ad.num_channels = 2; ad.logger.func = slog_func;
        ad.stream_cb = [](float* buf, int frames, int ch) {
            if (G && G->mixer && ch == 2) G->mixer->render(buf, frames); else std::memset(buf, 0, sizeof(float) * frames * ch);
        };
        saudio_setup(&ad);
        if (!saudio_isvalid()) VX_WARN("audio output unavailable; continuing without sound");
        applyOptions();
    }
    return true;
}

void sfx(const std::string& n, float gain) { if (G->mixer) G->mixer->play(n, SoundCat::Ui, gain); }
void sfx3d(const std::string& n, Vec3 at, SoundCat cat, float gain) { if (G->mixer) G->mixer->play3d(n, cat, at, gain, 0.9f + G->rng.unit() * 0.2f); }

bool startSession(const std::string& worldName, const NewWorld* create, std::string* err) {
    G->opt.world = worldName;
    resetPlayerState();
    G->entities = std::make_unique<EntityManager>(G->items);
    G->mobs = std::make_unique<MobSystem>(G->mobReg);
    G->entities->mobs = G->mobs.get();
    G->entities->onPickup = [](const ItemStack&) { sfx("pickup", 0.5f); };
    G->world = std::make_unique<World>(G->reg, *G->jobs, G->items.names());
    G->world->setEntityHost(G->entities.get());
    G->saveDir = userDataDir("Voxcraft") / "saves" / G->opt.world;
    if (fs::exists(G->saveDir / "world.json") && !(create && create->mustBeNew)) {
        if (!G->world->open(G->saveDir, err)) return false;
        auto pd = G->world->storage().readExtra("player.dat");
        if (!pd || !decodePlayer(*pd)) G->player.pos = surfaceSpawn();
        VX_INFO("opened world %s", G->saveDir.string().c_str());
    } else {
        if (!create) { if (err) *err = "world does not exist: " + worldName; return false; }
        WorldMeta m; m.name = create->displayName.empty() ? worldName : create->displayName;
        m.gen.seed = create->seedSet ? create->seed : (u64)time(nullptr) * 0x9E3779B97F4A7C15ull;
        if (create->flat) m.gen.preset = "flat";
        m.gameMode = create->creative ? "creative" : "survival";
        m.createdUnix = (u64)time(nullptr);
        auto gen = createGenerator(m.gen, G->reg);
        ColumnInfo c = gen->column(8, 8);
        m.spawn = {8, (m.gen.preset == "flat" ? c.height : std::max(c.height, SEA_LEVEL)) + 1, 8};   // above sea level, except flat worlds
        if (!G->world->create(G->saveDir, m, err)) return false;
        G->player.pos = surfaceSpawn();
        VX_INFO("created world %s (seed %llu)", G->saveDir.string().c_str(), (unsigned long long)m.gen.seed);
    }
    G->behaviors = std::make_unique<BlockBehaviors>(*G->world);
    G->behaviors->entityOnPlate = [](const BlockPos& b) {
        AABB plate{{(float)b.x, (float)b.y, (float)b.z}, {(float)b.x + 1, (float)b.y + 0.25f, (float)b.z + 1}};
        if (plate.intersects(G->player.box())) return true;
        for (auto& e : G->entities->items) if (plate.intersects({e.pos - Vec3(0.125f, 0, 0.125f), e.pos + Vec3(0.125f, 0.25f, 0.125f)})) return true;
        return false;
    };
    if (G->creative()) giveCreativeHotbar();
    G->player.canFly = G->creative();
    if (!G->creative()) G->player.flying = false;
    G->player.prevPos = G->player.pos;
    G->renderer.attach(G->world.get());
    return true;
}

bool saveWorld(bool sync) {
    if (!G->world) return false;
    std::string err;
    if (!G->world->save(sync, {{"player.dat", encodePlayer()}}, &err)) { VX_ERROR("save failed: %s", err.c_str()); return false; }
    return true;
}

void resetPlayerState() {
    G->player = Player{}; G->stats = PlayerStats{}; G->inv.clear(); G->hotbarSel = 0;
    G->cursor = {}; for (auto& c : G->craft) c = {}; G->craftResult = {};
    G->playerReady = false; G->target = {}; G->breakProgress = 0; G->accumulator = 0;
}

void endSession() {
    if (!G->world) return;
    if (G->mixer) G->mixer->stopAll();
    closeScreen();
    G->renderer.detach();
    saveWorld(true);
    G->behaviors.reset();
    G->world->close();
    G->world.reset();
    G->entities.reset();
    G->mobs.reset();
}

void respawn() {
    G->stats.respawn();
    G->player.pos = surfaceSpawn(); G->player.vel = {}; G->player.prevPos = G->player.pos; G->player.fallDistance = 0;
    G->playerReady = false;   // re-run the "find surface" step once the spawn chunk is loaded
    G->screen = Screen::None;
}

void simulateTick() {
    Player& p = G->player;
    bool inScreen = G->screen != Screen::None;
    PlayerInput in = G->input;
    if (!inScreen) {
        in.forward += (G->keys[SAPP_KEYCODE_W] ? 1.f : 0.f) - (G->keys[SAPP_KEYCODE_S] ? 1.f : 0.f);
        in.strafe = (G->keys[SAPP_KEYCODE_D] ? 1.f : 0.f) - (G->keys[SAPP_KEYCODE_A] ? 1.f : 0.f);
        in.jump = G->keys[SAPP_KEYCODE_SPACE]; in.sneak = G->keys[SAPP_KEYCODE_LEFT_SHIFT];
        in.sprint = (G->keys[SAPP_KEYCODE_LEFT_CONTROL] || G->keys[SAPP_KEYCODE_R]) && (G->creative() || G->stats.food > 6);
    }
    bool wasGround = p.onGround;
    Vec3 before = p.pos;
    if (!G->stats.dead) p.tick(*G->world, in);
    G->input.toggleFly = false;

    // survival bookkeeping
    bool creative = G->creative();
    if (!creative && !G->stats.dead) {
        float dx = p.pos.x - before.x, dz = p.pos.z - before.z, dist = std::sqrt(dx * dx + dz * dz);
        if (p.sprinting) G->stats.exhaust(0.1f * dist);
        if (in.jump && wasGround && p.vel.y > 0) G->stats.exhaust(p.sprinting ? 0.2f : 0.05f);
        if (p.landedFall >= 0 && !p.inFluid) G->stats.damage(fallDamage(p.landedFall), DamageSource::Fall);
        if (p.pos.y < -32) G->stats.damage(4, DamageSource::Void);
    }
    BlockState headB = G->world->getBlock((int)std::floor(p.eye().x), (int)std::floor(p.eye().y), (int)std::floor(p.eye().z));
    bool headWater = G->reg.fluid(headB) && G->reg.def(headB).name == "water";
    bool inLava = false;
    for (float yy : {0.1f, 1.0f}) { BlockState s = G->world->getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y + yy), (int)std::floor(p.pos.z)); if (G->reg.def(s).name == "lava") inLava = true; }
    G->stats.tick(headWater, inLava, creative);
    if (G->stats.dead && G->screen != Screen::Dead) {
        if (!creative) { std::vector<ItemStack> all = G->inv.slots; spill({(int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)}, all); G->inv.clear(); }
        closeScreen(); G->screen = Screen::Dead; G->mouseLocked = false; sapp_lock_mouse(false);
    }

    // ---- audio events
    if (!G->stats.dead) {
        float dx = p.pos.x - before.x, dz = p.pos.z - before.z;
        if (p.onGround && !p.sneaking) G->stepDistance += std::sqrt(dx * dx + dz * dz);
        if (G->stepDistance > 1.7f) {
            G->stepDistance = 0;
            BlockState below = G->world->getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y - 0.1f), (int)std::floor(p.pos.z));
            if (below != AIR) sfx3d("step_" + G->reg.def(below).sound, p.pos, SoundCat::Blocks, 0.35f);
        }
        if (p.inFluid && !G->wasInFluid && p.vel.y < -0.1f) sfx3d("splash", p.pos, SoundCat::Blocks, 0.7f);
        G->wasInFluid = p.inFluid;
        if (G->stats.health < G->lastHealth - 0.01f) sfx("hurt", 0.8f);
        G->lastHealth = G->stats.health;
        if (G->eatTicks > 0 && G->eatTicks % 8 == 1) sfx("eat", 0.5f);
    }
    for (auto& m : G->mobs->mobs) {
        Vec3 d = m.pos - p.pos;
        if (m.alive() && dot(d, d) < 20 * 20 && G->rng.range(500) == 0) sfx3d(G->mobReg.def(m.type).name + "_call", m.pos + Vec3(0, 1, 0), SoundCat::Creatures, 0.8f);
    }
    {
        double t = G->world->meta.tick / 20.0;
        bool underground = G->world->skyLight({(int)std::floor(p.pos.x), (int)std::floor(p.eye().y), (int)std::floor(p.pos.z)}) == 0 && p.pos.y < 55;
        if (underground && t > G->nextAmbient) { G->nextAmbient = t + 15 + G->rng.range(20); if (G->mixer) G->mixer->play(G->rng.chance(0.5f) ? "cave_drone" : "drip", SoundCat::Ambient, 0.4f); }
        if (t > G->nextMusic) { G->nextMusic = t + 300 + G->rng.range(300); if (G->mixer) G->mixer->startMusic(G->rng.next()); }
    }

    // eating: hold use with food selected
    const ItemDef* h = G->held();
    if (!inScreen && G->mouseDown[1] && h && h->isFood() && !creative && G->stats.food < 20) {
        if (++G->eatTicks >= 32) {
            G->eatTicks = 0; G->stats.eat(*h);
            ItemStack& s = G->inv.slots[G->hotbarSel]; s.count--; if (!s.count) s = {};
        }
    } else G->eatTicks = 0;
    if (G->useCooldown > 0) G->useCooldown--;
    if (!inScreen && G->mouseDown[1] && G->useCooldown == 0 && !(h && h->isFood())) { clickUse(); }

    G->entities->tick(*G->world, p, G->stats.dead ? nullptr : &G->inv);
    {   // pressure plates under the player or dropped items
        BlockPos feet{(int)std::floor(p.pos.x), (int)std::floor(p.pos.y + 0.05f), (int)std::floor(p.pos.z)};
        if (G->reg.def(G->world->getBlock(feet)).shape == BlockShape::Plate) G->behaviors->pressPlate(feet);
        for (auto& e : G->entities->items) {
            BlockPos ep{(int)std::floor(e.pos.x), (int)std::floor(e.pos.y + 0.05f), (int)std::floor(e.pos.z)};
            if (G->reg.def(G->world->getBlock(ep)).shape == BlockShape::Plate) G->behaviors->pressPlate(ep);
        }
    }
    MobContext ctx = mobContext();
    G->mobs->tick(ctx, G->rng);
    if (G->world->meta.tick % 20 == 0) G->mobs->spawnTick(*G->world, p, ctx.daylight, G->rng);
    if (G->attackCooldown > 0) G->attackCooldown--;
    tickKilns();
    G->world->tick();
}

MobContext mobContext() {
    float ang = (float)(G->world->meta.dayTime - 6000) / 24000.0f * 2 * PI;
    float daylight = clamp(0.5f + std::cos(ang) * 0.9f, 0.0f, 1.0f);
    MobContext c{*G->world, G->player, !G->creative() && !G->stats.dead, daylight, nullptr, nullptr};
    c.attackPlayer = [](Mob& m, float dmg) {
        if (G->stats.damage(dmg, DamageSource::Attack) > 0) {
            Vec3 k = normalize(Vec3(G->player.pos.x - m.pos.x, 0, G->player.pos.z - m.pos.z));
            G->player.vel.x += k.x * 0.5f; G->player.vel.z += k.z * 0.5f; G->player.vel.y = std::max(G->player.vel.y, 0.3f);
        }
    };
    c.dropLoot = [](const Mob& m, const std::vector<ItemStack>& loot) { for (auto& s : loot) G->entities->spawnItem(m.pos + Vec3(0, 0.5f, 0), s, G->rng); };
    return c;
}

bool attackMob() {
    if (G->attackCooldown > 0) return false;
    float md = 0;
    Mob* m = G->mobs->raycast(G->player.eye(), G->player.look(), G->creative() ? 5.0f : 3.5f, &md);
    if (!m) return false;
    if (G->target.hit && G->target.distance < md) return false;   // a block is in the way
    const ItemDef* h = G->held();
    float dmg = h ? h->damage : 1.0f;
    MobContext ctx = mobContext();
    if (G->mobs->hurt(*m, dmg, G->player.look(), G->rng, &ctx)) {
        sfx3d(m->alive() ? "mob_hurt" : "mob_death", m->pos + Vec3(0, 0.8f, 0), SoundCat::Creatures);
        G->stats.exhaust(0.1f);
        if (h && h->durability > 0) { damageHeldTool(); if (h->tool != ToolType::Sword) damageHeldTool(); }
    }
    G->attackCooldown = 8;
    return true;
}

void updateInteraction(float dt) {
    if (G->screen != Screen::None || !G->mouseLocked || !G->mouseDown[0] || !G->target.hit) { G->breakProgress = 0; G->breaking = {0, -1, 0}; return; }
    BlockPos t = G->target.block;
    BlockState s = G->world->getBlock(t);
    const BlockDef& d = G->reg.def(s);
    if (!(G->breaking == t)) { G->breaking = t; G->breakProgress = 0; }
    if (G->creative()) {
        if (G->breakProgress == 0) { breakAt(t, false); G->breakProgress = -0.25f; }   // repeat every 0.25 s while held
        else if ((G->breakProgress += dt) >= 0) G->breakProgress = 0;
        return;
    }
    float secs = breakSeconds(d, G->held(), G->player.onGround || G->player.flying, G->player.headInFluid);
    if (secs < 0) return;
    if ((G->digTimer -= dt) <= 0) { G->digTimer = 0.25f; sfx3d("dig_" + d.sound, {t.x + 0.5f, t.y + 0.5f, t.z + 0.5f}, SoundCat::Blocks, 0.6f); }
    G->breakProgress += secs == 0 ? 1.0f : dt / secs;
    if (G->breakProgress >= 1.0f) {
        breakAt(t, true);
        if (d.hardness > 0) damageHeldTool();
        G->stats.exhaust(0.005f);
        G->breakProgress = 0; G->breaking = {0, -1, 0};
        G->useCooldown = std::max(G->useCooldown, 0);
    }
}

void clickUse() {
    G->useCooldown = 4;
    if (G->target.hit && !G->keys[SAPP_KEYCODE_LEFT_SHIFT]) {
        BlockState s = G->world->getBlock(G->target.block);
        const BlockDef& d = G->reg.def(s);
        if (d.has(BF_INTERACT_CRAFT)) { openScreen(Screen::Workbench); return; }
        if (d.has(BF_INTERACT_CHEST)) { G->openBlock = G->target.block; G->world->ensureBlockEntity(G->openBlock, BE_CHEST)->items.resize(CHEST_SLOTS); openScreen(Screen::Chest); return; }
        if (d.has(BF_INTERACT_KILN)) { G->openBlock = G->target.block; G->world->ensureBlockEntity(G->openBlock, BE_KILN)->items.resize(KILN_SLOTS); openScreen(Screen::Kiln); return; }
        Vec3 tc{G->target.block.x + 0.5f, G->target.block.y + 0.5f, G->target.block.z + 0.5f};
        if (d.shape == BlockShape::Lever) { G->behaviors->toggleLever(G->target.block); sfx3d("click_circuit", tc, SoundCat::Blocks); return; }
        if (d.shape == BlockShape::Button) { G->behaviors->pressButton(G->target.block); sfx3d("click_circuit", tc, SoundCat::Blocks); return; }
        if (d.name == "relay") { G->world->setBlock(G->target.block, G->reg.withProp(s, "delay", (G->reg.getProp(s, "delay") + 1) % 4)); return; }
    }
    // buckets: scoop a source or pour one
    if (const ItemDef* h = G->held()) {
        ItemStack& hs = G->inv.slots[G->hotbarSel];
        Vec3 eye = G->player.eye();
        if (h->name == "bucket") {
            RayHit f = raycastBlocks(*G->world, eye, G->player.look(), 5.0f, true);
            BlockState s = f.hit ? G->world->getBlock(f.block) : AIR;
            if (f.hit && G->reg.fluid(s)) {
                bool lava = G->reg.def(s).name == "lava";
                G->world->setBlock(f.block, AIR);
                ItemStack full{G->items.id(lava ? "lava_bucket" : "water_bucket"), 1, 0};
                if (G->creative()) return;
                if (hs.count == 1) hs = full; else { hs.count--; giveOrDropInv(full); }
            }
            return;
        }
        if (h->name == "water_bucket" || h->name == "lava_bucket") {
            if (!G->target.hit) return;
            BlockPos at = G->target.block;
            if (!G->reg.def(G->world->getBlock(at)).has(BF_REPLACEABLE)) at = at + G->target.normal;
            if (!G->reg.def(G->world->getBlock(at)).has(BF_REPLACEABLE)) return;
            G->behaviors->placeFluidSource(at, h->name == "lava_bucket");
            if (!G->creative()) hs = {G->items.id("bucket"), 1, 0};
            return;
        }
    }
    placeHeld();
}

void pickBlock() {
    if (!G->target.hit) return;
    u16 it = G->items.forBlockType(G->reg.def(G->world->getBlock(G->target.block)).index);
    if (!it) return;
    for (int i = 0; i < 9; i++) if (G->inv.slots[i].item == it) { G->hotbarSel = i; return; }
    if (G->creative()) G->inv.slots[G->hotbarSel] = {it, (u8)G->items.maxStack(it), 0};
}

void dropSelected(bool whole) {
    ItemStack& s = G->inv.slots[G->hotbarSel];
    if (s.empty()) return;
    ItemStack d = s; d.count = whole ? s.count : 1;
    s.count = (u8)(s.count - d.count); if (!s.count) s = {};
    Vec3 l = G->player.look();
    G->entities->spawnItem(G->player.eye() + l * 0.3f - Vec3(0, 0.3f, 0), d, G->rng, l * 0.3f + Vec3(0, 0.1f, 0));
    G->entities->items.back().pickupDelay = 40;
}

} // namespace vx
