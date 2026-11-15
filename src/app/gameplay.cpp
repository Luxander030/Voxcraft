// Session lifecycle, persistence of player state, simulation tick and block interaction.
#include "game/rails.h"
#include "game/building.h"
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
    ByteWriter w; w.u8_(4);
    const Player& p = G->player;
    w.f64_(p.pos.x); w.f64_(p.pos.y); w.f64_(p.pos.z); w.f32_(p.yaw); w.f32_(p.pitch); w.u8_(p.flying ? 1 : 0); w.u8_((u8)G->hotbarSel);
    auto st = G->stats.encode(); w.var(st.size()); w.bytes(st.data(), st.size());
    w.var(G->inv.slots.size());
    for (auto& s : G->inv.slots) { w.str(s.empty() ? "" : G->items.def(s.item).name); w.u8_(s.empty() ? 0 : s.count); w.var(s.damage); }
    w.str(G->dimension);
    for (auto& s : G->armor) { w.str(s.empty() ? "" : G->items.def(s.item).name); w.var(s.damage); }
    return std::move(w.buf);
}

bool decodePlayer(const std::vector<u8>& b) {
    ByteReader r(b); u8 ver = r.u8_();
    if (ver < 1 || ver > 4) return false;
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
    G->pendingDimension = "overworld";
    if (ver >= 3) { std::string d = r.str(); if (r.ok() && !d.empty()) G->pendingDimension = d; }
    if (ver >= 4) for (auto& s : G->armor) { std::string n = r.str(); u16 dmg = (u16)r.var(); u16 id = G->items.id(n); s = id ? ItemStack{id, 1, dmg} : ItemStack{}; }
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
    if (d.shape != BlockShape::Fluid && d.shape != BlockShape::None)
        G->particles.blockDebris(pos, G->renderer.textures().face(s, FACE_NZ), d.tint == Tint::None ? 0xFFFFFFFFu : rgba(120, 175, 80), 24, G->rng);
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
    if (isBoxedShape(G->reg.def(place).shape)) {   // slabs, stairs, fences, panes, doors, trapdoors
        PlacePlan plan = planBuildingPlacement(*G->world, place, G->target.block, G->target.normal, G->target.point, G->player.look());
        if (!plan.ok) return false;
        AABB pb = G->player.box();
        for (auto [pos, st] : {std::pair<BlockPos, BlockState>{plan.at, plan.state}, {plan.at + BlockPos{0, 1, 0}, plan.upper}}) {
            if (st == AIR) continue;
            for (const ShapeBox& b : G->reg.collisionBoxes(st)) {
                AABB a{{pos.x + b.mn[0] / 16.0f, pos.y + b.mn[1] / 16.0f, pos.z + b.mn[2] / 16.0f}, {pos.x + b.mx[0] / 16.0f, pos.y + b.mx[1] / 16.0f, pos.z + b.mx[2] / 16.0f}};
                if (a.intersects(pb)) return false;
            }
        }
        if (!applyPlacement(*G->world, plan)) return false;
        const BlockDef& d = G->reg.def(place);
        sfx3d("place_" + d.sound, {plan.at.x + 0.5f, plan.at.y + 0.5f, plan.at.z + 0.5f}, SoundCat::Blocks);
        if (!G->creative()) { hs.count--; if (!hs.count) hs = {}; }
        return true;
    }
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
    if (d.name == "grain" && G->reg.def(below).name != "farmland") return false;   // crops need tilled soil
    if (d.has(BF_NEEDS_SOIL) && d.name != "grain") {
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
        } else if (cnt == 5) {   // hoppers point into the block that was clicked
            place = G->reg.withProp(place, "facing", n.y != 0 ? 0 : n.z > 0 ? 1 : n.z < 0 ? 2 : n.x > 0 ? 3 : 4);
        } else if (cnt == 4) {
            place = G->reg.withProp(place, "facing", horizontalFacingToward(G->player));
        } else if (cnt == 6) {
            Vec3 l = G->player.look(); int f;
            if (std::fabs(l.y) > 0.7f) f = l.y > 0 ? 0 : 1; else f = horizontalFacingToward(G->player) + 2;
            place = G->reg.withProp(place, "facing", f);
        }
    }
    if (d.shape == BlockShape::Rail) { Vec3 l = G->player.look(); place = G->reg.withProp(place, "shape", std::fabs(l.x) > std::fabs(l.z) ? 1 : 0); }
    if (!G->world->setBlock(at, place)) return false;
    sfx3d("place_" + d.sound, {at.x + 0.5f, at.y + 0.5f, at.z + 0.5f}, SoundCat::Blocks);
    if (int n = containerSlots(G->reg, place)) {   // every container gets its block entity when placed
        u8 kind = d.has(BF_INTERACT_KILN) ? BE_KILN : d.has(BF_INTERACT_CHEST) ? BE_CHEST : d.name == "hopper" ? BE_HOPPER : BE_DROPPER;
        G->world->ensureBlockEntity(at, kind)->items.resize(n);
    }
    if (!G->creative()) { hs.count--; if (!hs.count) hs = {}; }
    return true;
}

ContainerContext containerCtx() {
    ContainerContext c{*G->world, G->items, G->recipes};
    c.behaviors = G->behaviors.get(); c.entities = G->entities.get(); c.rng = &G->rng; c.carts = G->carts.get();
    c.shootArrow = [](Vec3 pos, Vec3 vel) { G->projectiles.shoot(ProjKind::Arrow, pos, vel, 2, true, true); };
    return c;
}

void setupBehaviors() {
    G->behaviors = std::make_unique<BlockBehaviors>(*G->world);
    G->behaviors->entityOnPlate = [](const BlockPos& b) {
        AABB plate{{(float)b.x, (float)b.y, (float)b.z}, {(float)b.x + 1, (float)b.y + 0.25f, (float)b.z + 1}};
        if (plate.intersects(G->player.box())) return true;
        for (auto& e : G->entities->items) if (plate.intersects({e.pos - Vec3(0.125f, 0, 0.125f), e.pos + Vec3(0.125f, 0.25f, 0.125f)})) return true;
        return false;
    };
    G->behaviors->cartOnRail = [](const BlockPos& b) {
        for (auto& c : G->carts->carts) { BlockPos cell; BlockState s; if (!c.removed && G->carts->railCell(*G->world, c.pos, cell, s) && cell == b) return true; }
        return false;
    };
    G->behaviors->containerSignal = [](const BlockPos& p) { ContainerContext c = containerCtx(); return containerSignal(c, p); };
    G->behaviors->onDispense = [](const BlockPos& p, bool d, const BlockPos& dir) {
        ContainerContext c = containerCtx();
        dispense(c, p, d, dir);
        sfx3d("click_circuit", {p.x + 0.5f, p.y + 0.5f, p.z + 0.5f}, SoundCat::Blocks, 0.7f);
    };
}
} // namespace

void giveOrDropInv(ItemStack s) {
    int left = G->inv.add(s, G->items);
    if (left) { s.count = (u8)left; G->entities->spawnItem(G->player.eye(), s, G->rng, G->player.look() * 0.3f); }
}

int armorPoints() { int p = 0; for (auto& a : G->armor) if (!a.empty()) p += G->items.def(a.item).armorPoints; return std::min(p, 20); }

float applyArmor(float dmg) {
    int pts = armorPoints();
    if (pts == 0 || dmg <= 0) return dmg;
    for (auto& a : G->armor) {   // every piece wears a little per hit
        if (a.empty()) continue;
        const ItemDef& d = G->items.def(a.item);
        if (++a.damage >= d.durability) { showStatus(d.name + " broke"); a = {}; }
    }
    return dmg * (1.0f - pts * 0.04f);   // up to 80% reduction at 20 points
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
        G->playerModel = G->mobReg.modelIndex("wanderer");
        if (G->playerModel < 0) { if (err) *err = "models.json: missing the wanderer (player) model"; return false; }
        if (const Json* rig = animJ.find("player")) { if (!loadClips(*rig, G->mobReg.model(G->playerModel), G->playerClips, err)) return false; }
        G->playerAnim.bind(&G->mobReg.model(G->playerModel), &G->playerClips);
    }
    G->particles.whiteLayer = (u16)std::max(0, G->pack.layer("snow"));
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

// Captions for sounds worth knowing about (accessibility: subtitles).
static const char* captionFor(const std::string& n) {
    static const std::pair<const char*, const char*> table[] = {
        {"grazer_call", "Grazer calls"}, {"puffbird_call", "Puffbird chirps"}, {"gloomling_call", "Gloomling growls"}, {"rockmite_call", "Rockmite clicks"},
        {"settler_call", "Settler hums"}, {"cinderwisp_call", "Cinderwisp crackles"}, {"mob_hurt", "Creature hurt"}, {"mob_death", "Creature dies"},
        {"hurt", "You are hurt"}, {"splash", "Splash"}, {"click_circuit", "Click"}, {"piston", "Piston moves"}, {"eat", "Eating"},
        {"rain", "Rain"}, {"cave_drone", "Cave rumbles"}, {"drip", "Water drips"}, {"pickup", "Item picked up"}};
    for (auto& [k, v] : table) if (n == k) return v;
    if (n.rfind("break_", 0) == 0) return "Block breaks";
    if (n.rfind("place_", 0) == 0) return "Block placed";
    return nullptr;
}
static void caption(const std::string& n, int side) {
    if (!G->options.subtitles) return;
    const char* c = captionFor(n);
    if (!c) return;
    for (auto& e : G->captions) if (e.text == c) { e.until = G->now + 3; e.side = side; return; }
    G->captions.push_back({c, G->now + 3, side});
    if (G->captions.size() > 6) G->captions.erase(G->captions.begin());
}
void sfx(const std::string& n, float gain) { if (G->mixer) G->mixer->play(n, SoundCat::Ui, gain); caption(n, 0); }
void sfx3d(const std::string& n, Vec3 at, SoundCat cat, float gain) {
    if (G->mixer) G->mixer->play3d(n, cat, at, gain, 0.9f + G->rng.unit() * 0.2f);
    Vec3 d = at - G->player.pos; if (dot(d, d) > 16 * 16) return;
    Vec3 right{std::cos(G->player.yaw), 0, std::sin(G->player.yaw)};
    float s = dot(normalize(d), right);
    caption(n, s > 0.3f ? 1 : s < -0.3f ? -1 : 0);
}

bool startSession(const std::string& worldName, const NewWorld* create, std::string* err) {
    G->opt.world = worldName;
    resetPlayerState();
    G->entities = std::make_unique<EntityManager>(G->items);
    G->mobs = std::make_unique<MobSystem>(G->mobReg);
    G->entities->mobs = G->mobs.get();
    G->carts = std::make_unique<CartSystem>();
    G->carts->onRail = [](const BlockPos& cell, BlockState s) { if (isDetectorRail(G->reg, s)) G->behaviors->cartOnDetector(cell); };
    G->entities->carts = G->carts.get();
    G->riding = 0;
    G->entities->onPickup = [](const ItemStack&) { sfx("pickup", 0.5f); };
    G->world = std::make_unique<World>(G->reg, *G->jobs, G->items.names());
    G->world->setEntityHost(G->entities.get());
    G->saveDir = userDataDir("Voxcraft") / "saves" / G->opt.world;
    G->dimension = "overworld"; G->pendingDimension = "overworld"; G->riftTicks = 0;
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
        m.gen.version = LATEST_GENERATOR_VERSION;   // existing worlds keep the version they were created with
        m.gameMode = create->creative ? "creative" : "survival";
        m.createdUnix = (u64)time(nullptr);
        auto gen = createGenerator(m.gen, G->reg);
        ColumnInfo c = gen->column(8, 8);
        m.spawn = {8, (m.gen.preset == "flat" ? c.height : std::max(c.height, SEA_LEVEL)) + 1, 8};   // above sea level, except flat worlds
        if (!G->world->create(G->saveDir, m, err)) return false;
        G->player.pos = surfaceSpawn();
        VX_INFO("created world %s (seed %llu)", G->saveDir.string().c_str(), (unsigned long long)m.gen.seed);
    }
    setupBehaviors();
    G->particles.parts.clear();
    if (G->creative()) giveCreativeHotbar();
    G->player.canFly = G->creative();
    if (!G->creative()) G->player.flying = false;
    G->player.prevPos = G->player.pos;
    G->renderer.attach(G->world.get());
    if (G->pendingDimension != "overworld") {   // the player was saved in another dimension
        if (!enterDimension(G->pendingDimension, G->player.pos, false, err)) return false;
    }
    return true;
}

bool saveWorld(bool sync) {
    if (!G->world) return false;
    std::string err;
    auto fail = [&](const std::string& e) { G->lastSaveError = e; VX_ERROR("save failed: %s", e.c_str()); return false; };
    if (G->dimension == "overworld") {
        if (!G->world->save(sync, {{"player.dat", encodePlayer()}}, &err)) return fail(err);
    } else {
        // another dimension's folder: its chunks go there, the player file always lives in the main save
        if (!G->world->save(sync, {}, &err)) return fail(err);
        auto pd = encodePlayer();
        if (!writeFileAtomic(G->saveDir / "player.dat", pd)) return fail("cannot write player.dat");
    }
    G->lastSaveError.clear();
    return true;
}

void showSaveFailure() { showStatus("Could not save the world: " + G->lastSaveError + ". Nothing was discarded."); }

bool enterDimension(const std::string& dim, Vec3 pos, bool arrive, std::string* err) {
    G->farEpoch++; G->farCX = 1 << 30;   // distant terrain belongs to the old world
    std::string mode = G->world ? G->world->meta.gameMode : "survival";
    GenSettings baseGen = G->world ? G->world->meta.gen : GenSettings{};
    if (G->world) {
        // Never close a world whose state is not safely on disk: if the save fails, stay put.
        const std::string from = G->dimension;
        G->dimension = dim;           // player.dat written below records the destination
        if (!saveWorld(true)) {
            G->dimension = from;
            if (err) *err = "cannot save before leaving: " + G->lastSaveError;
            return false;
        }
        G->renderer.detach();
        G->behaviors.reset();
        G->world->close();
        G->world.reset();
    }
    G->entities->items.clear(); G->mobs->mobs.clear(); G->carts->carts.clear(); G->riding = 0; G->particles.parts.clear(); G->projectiles.list.clear();
    fs::path dir = dim == "overworld" ? G->saveDir : G->saveDir / "dim" / dim;
    G->world = std::make_unique<World>(G->reg, *G->jobs, G->items.names());
    G->world->setEntityHost(G->entities.get());
    if (fs::exists(dir / "world.json")) {
        if (!G->world->open(dir, err)) return false;
    } else {
        WorldMeta m; m.name = dim; m.gen = baseGen; m.gen.dimension = dim; m.gen.preset = "default";
        m.gen.version = std::max<u32>(2, LATEST_GENERATOR_VERSION);   // the Hollow exists from generator v2 on
        m.gameMode = mode; m.createdUnix = (u64)time(nullptr); m.spawn = {0, 64, 0};
        std::error_code ec; fs::create_directories(dir.parent_path(), ec);
        if (!G->world->create(dir, m, err)) return false;
    }
    G->world->meta.gameMode = mode;
    G->dimension = dim;
    setupBehaviors();
    G->player.pos = pos; G->player.vel = {};
    if (arrive) {
        // find an existing rift near the target, or build a gate there
        BlockPos t{(int)std::floor(pos.x), (int)std::floor(pos.y), (int)std::floor(pos.z)};
        for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) G->world->loadChunkSync({(t.x >> 4) + dx, (t.z >> 4) + dz});
        u16 riftIdx = G->reg.byName("rift")->index;
        bool found = false; float best = 1e9f; BlockPos at;
        for (int x = t.x - 14; x <= t.x + 14; x++) for (int z = t.z - 14; z <= t.z + 14; z++) for (int y = 1; y < WORLD_H - 1; y++) {
            if (G->reg.def(G->world->getBlock(x, y, z)).index != riftIdx || G->reg.def(G->world->getBlock(x, y - 1, z)).index == riftIdx) continue;
            float d = (float)((x - t.x) * (x - t.x) + (z - t.z) * (z - t.z) + (y - t.y) * (y - t.y));
            if (d < best) { best = d; at = {x, y, z}; found = true; }
        }
        if (!found) {
            int y;
            if (dim == "overworld") y = std::max(G->world->surfaceY(t.x, t.z) + 1, SEA_LEVEL + 1);
            else {
                y = 64;
                for (int yy = 40; yy < 118; yy++) {
                    bool ok = G->reg.solid(G->world->getBlock(t.x, yy - 1, t.z));
                    for (int k = 0; k < 4 && ok; k++) ok = G->world->getBlock(t.x, yy + k, t.z) == AIR;
                    if (ok) { y = yy; break; }
                }
            }
            at = {t.x, y, t.z};
            G->behaviors->buildRiftGate(at, true);
        }
        G->player.pos = {at.x + 0.5f, (float)at.y, at.z + 0.5f};
        G->riftTicks = -1;   // must step out before the rift can take the player again
    }
    G->player.prevPos = G->player.pos;
    G->playerReady = false;
    G->renderer.attach(G->world.get());
    saveWorld(true);
    return true;
}

void resetPlayerState() {
    G->player = Player{}; G->stats = PlayerStats{}; G->inv.clear(); G->hotbarSel = 0;
    for (auto& a : G->armor) a = {};
    G->cursor = {}; for (auto& c : G->craft) c = {}; G->craftResult = {};
    G->playerReady = false; G->target = {}; G->breakProgress = 0; G->accumulator = 0;
}

bool endSession(bool force) {
    if (!G->world) return true;
    // Save first, while everything is still attached. If it fails, keep the session open so the
    // player's unsaved changes stay in memory (unless the process is exiting anyway).
    if (!saveWorld(true) && !force) return false;
    G->farEpoch++; G->farCX = 1 << 30;
    if (G->mixer) G->mixer->stopAll();
    closeScreen();
    G->renderer.detach();
    G->behaviors.reset();
    G->world->close();
    G->world.reset();
    G->entities.reset();
    G->mobs.reset();
    return true;
}

void respawn() {
    if (G->dimension != "overworld") { std::string err; enterDimension("overworld", surfaceSpawn(), false, &err); }
    G->stats.respawn();
    if (G->playerModel >= 0) G->playerAnim.bind(&G->mobReg.model(G->playerModel), &G->playerClips);   // leave the death pose
    G->player.pos = surfaceSpawn(); G->player.vel = {}; G->player.prevPos = G->player.pos; G->player.fallDistance = 0;
    G->playerReady = false;   // re-run the "find surface" step once the spawn chunk is loaded
    G->screen = Screen::None;
}

void simulateTick() {
    Player& p = G->player;
    bool inScreen = G->screen != Screen::None;
    PlayerInput in = G->input;
    if (!inScreen) {
        const UserOptions& o = G->options;
        auto down = [&](int a) { return G->keys[o.keys[a]]; };
        in.forward += (down(UserOptions::Forward) ? 1.f : 0.f) - (down(UserOptions::Back) ? 1.f : 0.f);
        in.strafe = (down(UserOptions::Right) ? 1.f : 0.f) - (down(UserOptions::Left) ? 1.f : 0.f);
        in.jump = down(UserOptions::Jump);
        in.sneak = o.toggleSneak ? G->sneakLatched : down(UserOptions::Sneak);
        in.sprint = (o.toggleSprint ? G->sprintLatched : (down(UserOptions::Sprint) || G->keys[SAPP_KEYCODE_R])) && (G->creative() || G->stats.food > 6);
        if (G->sprintLatched && in.forward <= 0) G->sprintLatched = false;   // toggled sprint ends when you stop
    }
    bool wasGround = p.onGround;
    Vec3 before = p.pos;
    Cart* ridden = G->riding ? G->carts->byId(G->riding) : nullptr;
    if (G->riding && (!ridden || in.sneak || G->stats.dead)) { dismountCart(); ridden = nullptr; }
    if (ridden) {   // the rider pushes gently in the view direction; the cart does the moving
        if (in.forward > 0) {
            Vec3 l = p.look(); Vec3 f = normalize(Vec3(l.x, 0, l.z));
            if (length(ridden->vel) < 0.15f) ridden->vel = ridden->vel + f * 0.006f;
        }
    } else if (!G->stats.dead) p.tick(*G->world, in);
    G->input.toggleFly = false;
    G->carts->tick(*G->world);
    if (G->openCart) {   // a storage cart that rolled away (or was destroyed) closes its screen
        Cart* oc = G->carts->byId(G->openCart);
        if (!oc || length(oc->pos - p.pos) > 6.0f) closeScreen();
    }
    if (Cart* rc = G->riding ? G->carts->byId(G->riding) : nullptr; rc && rc->onTrip) dismountCart();   // trip rail drops riders off
    if (!G->stats.dead) p.vel += collideCartsWithPlayer(*G->carts, p.box(), p.pos - before, G->riding);
    if (G->riding) {
        if (Cart* c = G->carts->byId(G->riding)) {
            p.prevPos = before; p.pos = c->pos + Vec3(0, 0.25f, 0); p.vel = {}; p.onGround = true; p.fallDistance = 0;
        } else dismountCart();
    }

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
        if (!creative) { std::vector<ItemStack> all = G->inv.slots; for (auto& a : G->armor) { all.push_back(a); a = {}; } spill({(int)std::floor(p.pos.x), (int)std::floor(p.pos.y), (int)std::floor(p.pos.z)}, all); G->inv.clear(); }
        closeScreen(); G->screen = Screen::Dead; G->mouseLocked = false; sapp_lock_mouse(false);
    }

    // ---- villages are populated lazily the first time a player comes near (never at generation)
    if (G->dimension == "overworld" && G->world->meta.tick % 40 == 0) {
        int vx, vy, vz;
        if (G->world->generator().nearestVillage((int)p.pos.x, (int)p.pos.z, 1, vx, vy, vz)) {
            float dx = vx - p.pos.x, dz = vz - p.pos.z;
            std::pair<i32, i32> reg{floorDiv(vx, 128), floorDiv(vz, 128)};
            auto& pv = G->world->meta.populatedVillages;
            if (dx * dx + dz * dz < 64 * 64 && G->world->isReady({vx >> 4, vz >> 4}) && std::find(pv.begin(), pv.end(), reg) == pv.end()) {
                u16 settler = G->mobReg.id("settler");
                int n = 3 + G->rng.range(3);
                for (int i = 0; i < n && settler != 0xFFFF; i++) {
                    int sx = vx + G->rng.range(9) - 4, sz = vz + G->rng.range(9) - 4;
                    if (std::abs(sx - vx) <= 1 && std::abs(sz - vz) <= 1) sx += 3;   // not in the well
                    Mob& m = G->mobs->spawn(settler, {sx + 0.5f, (float)(G->world->surfaceY(sx, sz) + 1), sz + 0.5f}, G->rng);
                    m.hasHome = true; m.home = {vx + 0.5f, (float)vy, vz + 0.5f}; m.seed = (u32)G->rng.next();
                }
                pv.push_back(reg);
            }
        }
    }

    // ---- weather (overworld): rain/snow spells with persistent timers
    if (G->dimension == "overworld") {
        WorldMeta& wm = G->world->meta;
        if (--wm.weatherTicks <= 0) { wm.raining = !wm.raining; wm.weatherTicks = wm.raining ? 6000 + G->rng.range(6000) : 12000 + G->rng.range(24000); }
        if (wm.raining) {
            Vec3 e = p.eye();
            for (int i = 0; i < 40; i++) {
                float x = e.x + (G->rng.unit() - 0.5f) * 28, z = e.z + (G->rng.unit() - 0.5f) * 28, y = e.y + 6 + G->rng.unit() * 8;
                if (G->world->surfaceY((int)std::floor(x), (int)std::floor(z)) >= y) continue;   // roofed or underground: dry
                Biome bi = G->world->generator().column((int)std::floor(x), (int)std::floor(z)).biome;
                bool snow = bi == Biome::Tundra || bi == Biome::SnowyPeaks || bi == Biome::FrozenOcean;
                G->particles.precipitation({x, y, z}, snow, G->rng);
            }
            bool exposed = G->world->skyLight({(int)std::floor(e.x), (int)std::floor(e.y), (int)std::floor(e.z)}) > 10;
            if (wm.tick % 80 == 0 && G->mixer) { G->mixer->play("rain", SoundCat::Ambient, exposed ? 0.5f : 0.2f); caption("rain", 0); }
        }
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
        if (p.inFluid && !G->wasInFluid && p.vel.y < -0.1f) {
            sfx3d("splash", p.pos, SoundCat::Blocks, 0.7f);
            G->particles.splash(p.pos, (u16)G->pack.layer("water"), rgba(70, 110, 230), G->rng);
        }
        G->wasInFluid = p.inFluid;
        if (G->stats.health < G->lastHealth - 0.01f) { sfx("hurt", 0.8f); G->playerAnim.play(AnimState::Hurt); }
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
    if (!inScreen && G->mouseDown[1] && G->useCooldown == 0 && !(h && (h->isFood() || h->name == "bow"))) { clickUse(); }

    // rift travel: stand in a rift for 4 s (0.5 s in creative)
    {
        u16 riftIdx = G->reg.byName("rift")->index;
        bool inRift = false;
        for (float yy : {0.2f, 1.2f}) inRift = inRift || G->reg.def(G->world->getBlock((int)std::floor(p.pos.x), (int)std::floor(p.pos.y + yy), (int)std::floor(p.pos.z))).index == riftIdx;
        if (!inRift) G->riftTicks = 0;
        else if (G->riftTicks >= 0 && ++G->riftTicks >= (G->creative() ? 10 : 80)) {
            std::string to = G->dimension == "overworld" ? "hollow" : "overworld";
            Vec3 target = to == "hollow" ? Vec3(std::floor(p.pos.x / 8), 64, std::floor(p.pos.z / 8)) : Vec3(std::floor(p.pos.x * 8), 64, std::floor(p.pos.z * 8));
            std::string err;
            sfx("splash", 0.4f);
            if (!enterDimension(to, target, true, &err)) { VX_ERROR("dimension travel failed: %s", err.c_str()); showStatus(G->lastSaveError.empty() ? "Travel failed" : "Travel cancelled: could not save the world"); }
            return;   // the world changed: nothing else this tick
        }
    }
    {   // breeding feedback: hearts drift up from creatures in love; a burst when a baby is born
        static int lastBirths = 0;
        if (G->world->meta.tick % 10 == 0)
            for (const Mob& m : G->mobs->mobs) if (m.loveTicks > 0) G->particles.puff(m.pos + Vec3(0, G->mobReg.def(m.type).height, 0), 1, rgba(240, 110, 150, 230), G->rng);
        if (G->mobs->births < lastBirths) lastBirths = 0;   // a new session's creature system
        if (G->mobs->births > lastBirths) {
            for (size_t k = G->mobs->mobs.size() - std::min<size_t>(G->mobs->mobs.size(), (size_t)(G->mobs->births - lastBirths)); k < G->mobs->mobs.size(); k++)
                G->particles.puff(G->mobs->mobs[k].pos + Vec3(0, 0.5f, 0), 10, rgba(240, 110, 150, 230), G->rng);
            lastBirths = G->mobs->births;
        }
    }
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
    collideCartsWithMobs(*G->carts, *G->mobs, G->riding);
    // particles: simulate, and let nearby torches emit flames (random sampling: no world scans)
    G->particles.tick(*G->world);
    for (int i = 0; i < 200; i++) {
        BlockPos q{(int)std::floor(p.pos.x) + G->rng.range(17) - 8, (int)std::floor(p.pos.y) + G->rng.range(17) - 8, (int)std::floor(p.pos.z) + G->rng.range(17) - 8};
        BlockState s = G->world->getBlock(q);
        if (s == AIR) continue;
        const BlockDef& td = G->reg.def(s);
        if (td.shape == BlockShape::Torch && (td.propIndex("lit") < 0 || G->reg.getProp(s, "lit"))) {
            AABB b = blockShapeBounds(G->reg, s);
            G->particles.flame({q.x + (b.min.x + b.max.x) / 2, q.y + b.max.y, q.z + (b.min.z + b.max.z) / 2}, G->renderer.textures().face(s, FACE_PY), G->rng);
        }
    }
    if (G->world->meta.tick % 20 == 0) G->mobs->spawnTick(*G->world, p, ctx.daylight, G->rng, 10, 12, G->dimension);
    // projectiles
    {
        ProjectileHooks h;
        h.hitMob = [](Mob& m, float dmg, Vec3 dir) {
            MobContext mc = mobContext();
            if (G->mobs->hurt(m, dmg, dir, G->rng, &mc)) sfx3d(m.alive() ? "mob_hurt" : "mob_death", m.pos + Vec3(0, 0.8f, 0), SoundCat::Creatures);
            if (!m.alive()) G->particles.puff(m.pos, 12, rgba(220, 220, 220, 220), G->rng);
        };
        h.hitPlayer = [](float dmg, Vec3 dir) {
            if (G->creative()) return;
            if (G->stats.damage(applyArmor(dmg), DamageSource::Attack) > 0) { G->player.vel.x += dir.x * 0.4f; G->player.vel.z += dir.z * 0.4f; G->player.vel.y = std::max(G->player.vel.y, 0.25f); }
        };
        h.pickupArrow = [] { if (G->creative()) return true; return G->inv.add({G->items.id("arrow"), 1, 0}, G->items) == 0; };
        h.impact = [](const Projectile& pr) {
            if (pr.kind == ProjKind::EmberBolt) { for (int i = 0; i < 4; i++) G->particles.flame(pr.pos, (u16)std::max(0, G->pack.layer("cinderwisp_core")), G->rng); sfx3d("break_stone", pr.pos, SoundCat::Creatures, 0.5f); }
            else sfx3d("dig_wood", pr.pos, SoundCat::Blocks, 0.5f);
        };
        G->projectiles.tick(*G->world, *G->mobs, p, h);
        for (auto& pr : G->projectiles.list) if (pr.kind == ProjKind::EmberBolt && G->rng.chance(0.5f)) G->particles.flame(pr.pos, (u16)std::max(0, G->pack.layer("cinderwisp_core")), G->rng);
    }
    // bow: hold use to draw, release to loose an arrow
    {
        const ItemDef* hb = G->held();
        bool right = !inScreen && G->mouseDown[1];
        bool isBow = hb && hb->name == "bow";
        bool hasArrow = G->creative() || G->inv.count(G->items.id("arrow")) > 0;
        if (isBow && right && hasArrow) G->bowCharge = std::min(G->bowCharge + 1, 20);
        else if (isBow && G->prevRight && !right && G->bowCharge > 2) {
            float t = G->bowCharge / 20.0f, power = (t * t + 2 * t) / 3;
            Vec3 l = G->player.look();
            G->projectiles.shoot(ProjKind::Arrow, G->player.eye() + l * 0.4f, l * (3.0f * power), 2, true, !G->creative());
            if (!G->creative()) { G->inv.remove(G->items.id("arrow"), 1); damageHeldTool(); }
            sfx3d("dig_wood", G->player.eye(), SoundCat::Blocks, 0.6f);
            G->bowCharge = 0;
        } else if (!right) G->bowCharge = 0;
        G->prevRight = right;
    }
    if (G->attackCooldown > 0) G->attackCooldown--;
    { ContainerContext cc = containerCtx(); tickContainers(cc, {(int)std::floor(p.pos.x) >> 4, (int)std::floor(p.pos.z) >> 4}, G->world->simulationRadius); }
    G->world->tick();
}

MobContext mobContext() {
    float ang = (float)(G->world->meta.dayTime - 6000) / 24000.0f * 2 * PI;
    float daylight = clamp(0.5f + std::cos(ang) * 0.9f, 0.0f, 1.0f);
    if (G->dimension == "hollow") daylight = 0;
    MobContext c{*G->world, G->player, !G->creative() && !G->stats.dead, daylight, nullptr, nullptr};
    c.attackPlayer = [](Mob& m, float dmg) {
        if (G->stats.damage(applyArmor(dmg), DamageSource::Attack) > 0) {
            Vec3 k = normalize(Vec3(G->player.pos.x - m.pos.x, 0, G->player.pos.z - m.pos.z));
            G->player.vel.x += k.x * 0.5f; G->player.vel.z += k.z * 0.5f; G->player.vel.y = std::max(G->player.vel.y, 0.3f);
        }
    };
    c.dropLoot = [](const Mob& m, const std::vector<ItemStack>& loot) { for (auto& s : loot) G->entities->spawnItem(m.pos + Vec3(0, 0.5f, 0), s, G->rng); };
    c.shoot = [](Mob& m, Vec3 from, Vec3 vel) {
        G->projectiles.shoot(ProjKind::EmberBolt, from, vel, G->mobReg.def(m.type).damage, false, false);
        sfx3d("mob_hurt", from, SoundCat::Creatures, 0.4f);
    };
    c.dimension = G->dimension;
    return c;
}

void dismountCart() {
    Cart* c = G->carts ? G->carts->byId(G->riding) : nullptr;
    G->riding = 0;
    if (!c) return;
    Player& p = G->player;
    // step off to the side of the track if there is room, else stand on top of the cart
    Vec3 side = Vec3(std::cos(c->yaw), 0, std::sin(c->yaw));
    for (Vec3 cand : {c->pos + side * 1.0f, c->pos - side * 1.0f, c->pos + Vec3(0, 0.8f, 0)}) {
        if (!collidesAny(*G->world, p.boxAt(cand))) { p.pos = p.prevPos = cand; break; }
    }
    p.vel = {};
}

bool attackCart() {
    float cd = 0;
    Cart* c = G->carts->raycast(G->player.eye(), G->player.look(), G->creative() ? 5.0f : 3.5f, &cd);
    if (!c || (G->target.hit && G->target.distance < cd)) return false;
    c->hurtTicks = 10;
    sfx3d("place_metal", c->pos + Vec3(0, 0.4f, 0), SoundCat::Blocks, 0.7f);
    if (++c->damage >= 3 || G->creative()) {
        if (c->id == G->riding) dismountCart();
        if (c->id == G->openCart) closeScreen();
        for (const ItemStack& s : cartDrops(*c, G->items, G->creative())) G->entities->spawnItem(c->pos + Vec3(0, 0.4f, 0), s, G->rng);
        c->removed = true;
    }
    return true;
}

bool attackMob() {
    if (attackCart()) return true;
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
        if (!m->alive()) G->particles.puff(m->pos, 12, rgba(220, 220, 220, 220), G->rng);
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
    if ((G->digTimer -= dt) <= 0) {
        G->digTimer = 0.25f; sfx3d("dig_" + d.sound, {t.x + 0.5f, t.y + 0.5f, t.z + 0.5f}, SoundCat::Blocks, 0.6f);
        const BlockPos& n = G->target.normal;
        G->particles.blockDebris(t, G->renderer.textures().face(s, FACE_NZ), d.tint == Tint::None ? 0xFFFFFFFFu : rgba(120, 175, 80), 3, G->rng, Vec3((float)n.x, (float)n.y, (float)n.z) * 0.08f);
    }
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
    G->swingRequested = true;
    {   // talk to a settler
        float md = 0;
        if (Mob* m = G->mobs->raycast(G->player.eye(), G->player.look(), 4.0f, &md); m && G->mobReg.def(m->type).trader && (!G->target.hit || md < G->target.distance)) {
            G->tradeSeed = m->seed; G->tradePos = m->pos;
            openScreen(Screen::Trade);
            sfx3d("grazer_call", m->pos + Vec3(0, 1.6f, 0), SoundCat::Creatures, 0.6f);
            return;
        }
        ItemStack& hs = G->inv.slots[G->hotbarSel];
        // carts: put one on a rail, or climb into one
        float cd = 0;
        Cart* cart = G->carts->raycast(G->player.eye(), G->player.look(), 4.0f, &cd);
        CartKind heldKind = CartKind::Plain;
        bool holdingCart = !hs.empty() && cartKindFromItem(G->items.def(hs.item).name, heldKind);
        if (cart && !holdingCart && (!G->target.hit || cd < G->target.distance)) {
            if (!cart->items.empty()) { G->openCart = cart->id; openScreen(Screen::Chest); }
            else if (!cart->carriesMob) G->riding = cart->id;
            return;
        }
        if (holdingCart && G->target.hit && isRail(G->reg, G->world->getBlock(G->target.block))) {
            const BlockPos& b = G->target.block;
            G->carts->spawn({b.x + 0.5f, (float)b.y, b.z + 0.5f}, heldKind);
            sfx3d("place_metal", {b.x + 0.5f, b.y + 0.2f, b.z + 0.5f}, SoundCat::Blocks);
            if (!G->creative()) { hs.count--; if (!hs.count) hs = {}; }
            return;
        }
        // feed a creature its food: love mode (or a growth spurt for a baby)
        if (Mob* m = G->mobs->raycast(G->player.eye(), G->player.look(), 4.0f, &md); m && !hs.empty() && (!G->target.hit || md < G->target.distance)) {
            if (G->mobs->feed(*m, hs.item)) {
                if (!G->creative()) { hs.count--; if (!hs.count) hs = {}; }
                G->particles.puff(m->pos + Vec3(0, G->mobReg.def(m->type).height * m->scale(), 0), 6, rgba(240, 110, 150, 230), G->rng);
                sfx("eat", 0.5f);
                return;
            }
        }
    }
    if (G->target.hit && !G->keys[G->options.keys[UserOptions::Sneak]] && !G->sneakLatched) {
        BlockState s = G->world->getBlock(G->target.block);
        const BlockDef& d = G->reg.def(s);
        if (isOpenable(G->reg, s) && toggleOpenable(*G->world, G->target.block)) {
            sfx3d("place_" + d.sound, {G->target.block.x + 0.5f, G->target.block.y + 0.5f, G->target.block.z + 0.5f}, SoundCat::Blocks, 0.8f);
            return;
        }
        if (d.has(BF_INTERACT_CRAFT)) { openScreen(Screen::Workbench); return; }
        if (d.has(BF_INTERACT_CHEST)) {
            G->openBlock = G->target.block;
            bool fresh = G->world->blockEntity(G->openBlock) == nullptr;
            BlockEntity* be = G->world->ensureBlockEntity(G->openBlock, BE_CHEST);
            be->items.resize(CHEST_SLOTS);
            if (fresh) rollGeneratedChestLoot(*G->world, G->recipes, G->openBlock, *be);   // part of the generated terrain
            openScreen(Screen::Chest); return;
        }
        if (d.has(BF_INTERACT_CONTAINER)) {
            G->openBlock = G->target.block;
            if (!G->world->blockEntity(G->openBlock)) G->world->ensureBlockEntity(G->openBlock, d.name == "hopper" ? BE_HOPPER : BE_DROPPER)->items.resize(containerSlots(G->reg, s));
            openScreen(Screen::Chest); return;
        }
        if (d.has(BF_INTERACT_KILN)) { G->openBlock = G->target.block; G->world->ensureBlockEntity(G->openBlock, BE_KILN)->items.resize(KILN_SLOTS); openScreen(Screen::Kiln); return; }
        Vec3 tc{G->target.block.x + 0.5f, G->target.block.y + 0.5f, G->target.block.z + 0.5f};
        if (d.name == "bedroll") {
            i64 t = G->world->meta.dayTime;
            if (G->dimension != "overworld") { showStatus("You cannot sleep here"); return; }
            if (t < 12500 || t > 23500) { showStatus("You can only sleep at night"); return; }
            for (auto& m : G->mobs->mobs) { Vec3 dd = m.pos - G->player.pos; if (G->mobReg.def(m.type).behavior == MobBehavior::Hostile && dot(dd, dd) < 10 * 10) { showStatus("Creatures are nearby"); return; } }
            G->world->meta.spawn = {G->target.block.x, G->target.block.y + 1, G->target.block.z};
            G->world->meta.dayTime = 1000; G->world->meta.raining = false;
            showStatus("You slept until morning. Spawn point set");
            return;
        }
        if (d.name == "comparator") { G->behaviors->toggleComparator(G->target.block); sfx3d("click_circuit", tc, SoundCat::Blocks); return; }
        if (d.shape == BlockShape::Lever) { G->behaviors->toggleLever(G->target.block); sfx3d("click_circuit", tc, SoundCat::Blocks); return; }
        if (d.shape == BlockShape::Button) { G->behaviors->pressButton(G->target.block); sfx3d("click_circuit", tc, SoundCat::Blocks); return; }
        if (d.name == "relay") { G->world->setBlock(G->target.block, G->reg.withProp(s, "delay", (G->reg.getProp(s, "delay") + 1) % 4)); return; }
    }
    // lily pads go on the surface of still water
    if (const ItemDef* h = G->held(); h && h->name == "lilypad") {
        RayHit f = raycastBlocks(*G->world, G->player.eye(), G->player.look(), 5.0f, true);
        if (f.hit && G->reg.def(G->world->getBlock(f.block)).name == "water" && G->world->getBlock({f.block.x, f.block.y + 1, f.block.z}) == AIR) {
            G->world->setBlock({f.block.x, f.block.y + 1, f.block.z}, G->reg.stateOf("lilypad"));
            sfx3d("place_grass", {f.block.x + 0.5f, f.block.y + 1.0f, f.block.z + 0.5f}, SoundCat::Blocks);
            if (!G->creative()) { ItemStack& s = G->inv.slots[G->hotbarSel]; s.count--; if (!s.count) s = {}; }
        }
        return;
    }
    // hoe: till turf/dirt with air above into farmland
    if (const ItemDef* h = G->held(); h && h->tool == ToolType::Hoe && G->target.hit) {
        BlockPos t = G->target.block;
        const std::string& n = G->reg.def(G->world->getBlock(t)).name;
        if ((n == "turf" || n == "dirt") && G->world->getBlock({t.x, t.y + 1, t.z}) == AIR) {
            G->world->setBlock(t, G->reg.stateOf("farmland"));
            sfx3d("dig_gravel", {t.x + 0.5f, t.y + 1.0f, t.z + 0.5f}, SoundCat::Blocks);
            damageHeldTool();
            return;
        }
    }
    // ember striker: light a rift inside a slate frame
    if (const ItemDef* h = G->held(); h && h->name == "ember_striker" && G->target.hit) {
        if (G->behaviors->lightRift(G->target.block + G->target.normal) > 0) {
            sfx3d("break_glass", {G->target.block.x + 0.5f, G->target.block.y + 1.0f, G->target.block.z + 0.5f}, SoundCat::Blocks);
            damageHeldTool();
        } else showStatus("Needs a closed slate frame");
        return;
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
