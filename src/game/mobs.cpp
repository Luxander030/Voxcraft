#include "game/mobs.h"
#include <queue>

namespace vx {

// ------------------------------------------------------------------ registry
bool MobRegistry::load(const Json& mobs, const Json& models, const Json& anims, const ItemRegistry& items, const TexturePackNames* tex, std::string* err) {
    defs_.clear(); models_.clear(); clips_.clear();
    std::unordered_map<std::string, int> modelIndex;
    for (auto& [name, mj] : models.obj) {
        Model m; if (!loadModel(mj, tex, m, err)) return false;
        modelIndex[name] = (int)models_.size(); models_.push_back(std::move(m));
    }
    for (auto& [name, j] : mobs.obj) {
        MobDef d; d.name = name; d.id = (u16)defs_.size(); d.rig = j["rig"].asString();
        auto mi = modelIndex.find(j["model"].asString());
        if (mi == modelIndex.end()) { if (err) *err = "mob " + name + ": unknown model " + j["model"].asString(); return false; }
        d.model = mi->second;
        d.health = (float)j["health"].asNumber(10); d.speed = (float)j["speed"].asNumber(0.1); d.width = (float)j["width"].asNumber(0.6);
        d.height = (float)j["height"].asNumber(1.8); d.damage = (float)j["damage"].asNumber(0);
        d.behavior = j["behavior"].asString() == "hostile" ? MobBehavior::Hostile : MobBehavior::Passive;
        d.burnsInDaylight = j["burns_in_daylight"].asBool(false);
        d.flying = j["flying"].asBool(false); d.ranged = j["ranged"].asBool(false); d.emissive = j["emissive"].asBool(false);
        if (const Json* dims = j.find("dimensions")) { d.dimensions.clear(); for (auto& x : dims->arr) d.dimensions.push_back(x.asString()); }
        for (auto& dr : j["drops"].arr) {
            u16 it = items.id(dr["item"].asString());
            if (!it) { if (err) *err = "mob " + name + ": unknown drop " + dr["item"].asString(); return false; }
            d.drops.push_back({it, dr["min"].asInt(0), dr["max"].asInt(1)});
        }
        const Json& sp = j["spawn"];
        std::string w = sp["where"].asString("surface");
        d.where = w == "dark" ? SpawnWhere::Dark : w == "underground" ? SpawnWhere::Underground : w == "air" ? SpawnWhere::Air : SpawnWhere::Surface;
        d.groupMin = sp["group"][0].asInt(1); d.groupMax = sp["group"][1].asInt(1); d.weight = sp["weight"].asInt(1);
        std::unordered_map<std::string, Clip> clips;
        if (const Json* rig = anims.find(d.rig)) if (!loadClips(*rig, models_[d.model], clips, err)) return false;
        clips_.push_back(std::move(clips));
        defs_.push_back(std::move(d));
    }
    return true;
}

// ------------------------------------------------------------------ pathfinding
bool walkableCell(const World& w, const BlockPos& p, int h) {
    if (p.y <= 0 || p.y + h >= WORLD_H) return false;
    for (int k = 0; k < h; k++) {
        BlockState s = w.getBlock(p.x, p.y + k, p.z);
        if (w.reg.solid(s) || w.reg.def(s).has(BF_HOT) || w.reg.def(s).name == "lava") return false;
    }
    BlockState below = w.getBlock(p.x, p.y - 1, p.z);
    return w.reg.solid(below) || w.reg.fluid(w.getBlock(p.x, p.y, p.z));
}

std::vector<BlockPos> findPath(const World& w, BlockPos start, BlockPos goal, int h, int maxNodes) {
    struct Node { BlockPos p; int g, f; u32 seq; int parent; };
    auto key = [](const BlockPos& p) { return ((u64)(u32)(p.x + 1000000) << 40) ^ ((u64)(u32)p.y << 30) ^ (u64)(u32)(p.z + 1000000); };
    std::vector<Node> nodes;
    std::unordered_map<u64, int> best;
    auto cmp = [&](int a, int b) {   // min-heap on (f, g desc, seq): deterministic
        const Node &x = nodes[a], &y = nodes[b];
        if (x.f != y.f) return x.f > y.f;
        if (x.g != y.g) return x.g < y.g;
        return x.seq > y.seq;
    };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> open(cmp);
    auto hcost = [&](const BlockPos& p) { return std::abs(p.x - goal.x) + std::abs(p.y - goal.y) + std::abs(p.z - goal.z); };
    u32 seq = 0;
    nodes.push_back({start, 0, hcost(start), seq++, -1}); best[key(start)] = 0; open.push(0);
    static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    int expanded = 0;
    while (!open.empty() && expanded < maxNodes) {
        int ci = open.top(); open.pop();
        Node cur = nodes[ci];
        if (best[key(cur.p)] < cur.g) continue;
        if (cur.p == goal || (std::abs(cur.p.x - goal.x) + std::abs(cur.p.z - goal.z) <= 1 && std::abs(cur.p.y - goal.y) <= 1)) {
            std::vector<BlockPos> path;
            for (int i = ci; i >= 0; i = nodes[i].parent) path.push_back(nodes[i].p);
            std::reverse(path.begin(), path.end());
            return path;
        }
        expanded++;
        for (auto& d : D) {
            for (int dy : {0, 1, -1, -2, -3}) {
                BlockPos n{cur.p.x + d[0], cur.p.y + dy, cur.p.z + d[1]};
                if (dy == 1 && w.reg.solid(w.getBlock(cur.p.x, cur.p.y + h, cur.p.z))) continue;   // no head room to jump
                if (dy < 0) {   // falling: the column we drop through must be clear
                    bool clear = true;
                    for (int k = 0; k > dy && clear; k--) for (int hh = 0; hh < h; hh++) if (w.reg.solid(w.getBlock(n.x, cur.p.y + k + hh, n.z))) clear = false;
                    if (!clear) continue;
                }
                if (!walkableCell(w, n, h)) continue;
                int g = cur.g + 1 + (dy > 0 ? 1 : 0);
                auto it = best.find(key(n));
                if (it != best.end() && it->second <= g) break;
                best[key(n)] = g;
                nodes.push_back({n, g, g + hcost(n), seq++, ci});
                open.push((int)nodes.size() - 1);
                break;   // only one vertical option per direction (the first walkable)
            }
        }
    }
    return {};
}

// ------------------------------------------------------------------ simulation
Mob& MobSystem::spawn(u16 type, Vec3 pos, Rng& rng) {
    Mob m; m.type = type; m.pos = m.prevPos = pos; m.health = reg_.def(type).health;
    m.yaw = m.prevYaw = rng.unit() * 2 * PI;
    m.anim.bind(&reg_.model(reg_.def(type).model), &reg_.clips(type));
    mobs.push_back(std::move(m));
    return mobs.back();
}

void MobSystem::move(const World& w, Mob& m, const MobDef& d) {
    bool inFluid = w.reg.fluid(w.getBlock((int)std::floor(m.pos.x), (int)std::floor(m.pos.y + 0.2f), (int)std::floor(m.pos.z)));
    if (d.flying && m.alive()) m.vel.y *= 0.85f;   // fliers hold altitude (vertical intent set by the AI)
    else m.vel.y = inFluid ? std::min(m.vel.y * 0.8f + 0.03f, 0.08f) : std::max((m.vel.y - 0.078f) * 0.98f, -3.0f);
    float hw = d.width * 0.5f;
    bool hitX = false, hitZ = false, hitY;
    auto boxAt = [&]() { return AABB{{m.pos.x - hw, m.pos.y, m.pos.z - hw}, {m.pos.x + hw, m.pos.y + d.height, m.pos.z + hw}}; };
    float dy = sweepAxis(w, boxAt(), 1, m.vel.y, hitY); m.pos.y += dy;
    m.onGround = hitY && m.vel.y < 0;
    if (hitY) m.vel.y = 0;
    float dx = sweepAxis(w, boxAt(), 0, m.vel.x, hitX); m.pos.x += dx;
    float dz = sweepAxis(w, boxAt(), 2, m.vel.z, hitZ); m.pos.z += dz;
    if ((hitX || hitZ) && m.onGround && !d.flying) m.vel.y = 0.42f;   // auto-jump over 1-block steps
    float fr = d.flying ? 0.85f : m.onGround ? 0.55f : 0.91f;
    m.vel.x *= fr; m.vel.z *= fr;
}

int MobSystem::countNear(Vec3 p, float r, MobBehavior b) const {
    int n = 0;
    for (auto& m : mobs) { Vec3 d = m.pos - p; if (!m.removed && reg_.def(m.type).behavior == b && dot(d, d) < r * r) n++; }
    return n;
}

void MobSystem::killDrops(Mob& m, Rng& rng, MobContext* ctx) {
    const MobDef& d = reg_.def(m.type);
    std::vector<ItemStack> loot;
    for (auto& dr : d.drops) { int n = dr.min + rng.range(dr.max - dr.min + 1); if (n > 0) loot.push_back({dr.item, (u8)n, 0}); }
    if (ctx && ctx->dropLoot) ctx->dropLoot(m, loot);
}

bool MobSystem::hurt(Mob& m, float dmg, Vec3 dir, Rng& rng, MobContext* ctx) {
    if (!m.alive() || m.hurtTicks > 0) return false;
    m.health -= dmg; m.hurtTicks = 10;
    Vec3 k = normalize(Vec3(dir.x, 0, dir.z));
    m.vel.x += k.x * 0.4f; m.vel.z += k.z * 0.4f; m.vel.y = std::max(m.vel.y, 0.3f);
    if (reg_.def(m.type).behavior == MobBehavior::Passive) { m.panicTicks = 100; m.path.clear(); }
    if (m.health <= 0) { m.deathTicks = 20; m.anim.play(AnimState::Death); killDrops(m, rng, ctx); }
    else m.anim.play(AnimState::Hurt);
    return true;
}

void MobSystem::tick(MobContext& ctx, Rng& rng) {
    const World& w = ctx.world;
    Vec3 pp = ctx.player.pos;
    for (auto& m : mobs) {
        if (m.removed) continue;
        const MobDef& d = reg_.def(m.type);
        if (!w.isReady({(int)std::floor(m.pos.x) >> 4, (int)std::floor(m.pos.z) >> 4})) continue;   // frozen at the world edge
        Vec3 toP = pp - m.pos; float distP = std::sqrt(toP.x * toP.x + toP.z * toP.z);
        if (d.behavior == MobBehavior::Hostile) {   // despawn far hostiles
            if (distP > 96 || (distP > 48 && rng.range(800) == 0)) { m.removed = true; continue; }
        }
        float frozenR = (float)(w.simulationRadius * 16 + 8);
        if (distP > frozenR) continue;
        m.prevPos = m.pos; m.prevYaw = m.yaw; m.age++;
        if (m.hurtTicks > 0) m.hurtTicks--;
        if (m.attackCooldown > 0) m.attackCooldown--;
        if (!m.alive()) { if (--m.deathTicks <= 0) m.removed = true; move(w, m, d); continue; }
        if (m.pos.y < -32) { m.removed = true; continue; }

        // daylight burn
        if (d.burnsInDaylight && ctx.daylight > 0.6f && w.skyLight({(int)std::floor(m.pos.x), (int)std::floor(m.pos.y + d.height), (int)std::floor(m.pos.z)}) >= 12) {
            if (++m.burnTimer >= 20) { m.burnTimer = 0; m.hurtTicks = 0; hurt(m, 2, Vec3(0, 0, 0.001f), rng, &ctx); }
        }

        // ---- flying ranged attackers: hover at range, strafe, shoot with line of sight
        if (d.flying) {
            Vec3 eye = m.pos + Vec3(0, d.height * 0.6f, 0), target = pp + Vec3(0, 1.3f, 0);
            Vec3 to = target - eye; float dist = length(to);
            Vec3 want{0, 0, 0};
            bool engaged = ctx.playerTargetable && dist < 24;
            if (engaged) {
                Vec3 flat = normalize(Vec3(to.x, 0, to.z)), side{-flat.z, 0, flat.x};
                want = dist > 10 ? flat : dist < 5 ? flat * -1.0f : side * (std::sin(m.age * 0.03f) > 0 ? 1.0f : -1.0f);
                float altWant = pp.y + 3.0f;
                m.vel.y += clamp((altWant - m.pos.y) * 0.02f, -0.04f, 0.04f);
                if (++m.pathTimer >= 60 && d.ranged && ctx.shoot) {
                    RayHit los = raycastBlocks(w, eye, to, dist);
                    if (!los.hit) { m.pathTimer = 0; m.anim.play(AnimState::Attack); ctx.shoot(m, eye + normalize(to) * 0.6f, normalize(to) * 0.9f); }
                }
                m.yaw = std::atan2(to.x, -to.z);
            } else {
                if (--m.idleTimer <= 0) { m.idleTimer = 80 + rng.range(80); m.headYaw = rng.unit() * 2 * PI; }
                want = Vec3(std::sin(m.headYaw), 0, -std::cos(m.headYaw)) * 0.5f;
                m.vel.y += (rng.unit() - 0.5f) * 0.01f;
                m.yaw += clamp((float)std::remainder(m.headYaw - m.yaw, 2 * PI), -0.1f, 0.1f);
            }
            m.vel.x += (want.x * d.speed - m.vel.x) * 0.3f; m.vel.z += (want.z * d.speed - m.vel.z) * 0.3f;
            move(w, m, d);
            continue;
        }

        // ---- decide a goal
        Vec3 want{0, 0, 0}; float speed = d.speed;
        BlockPos feet{(int)std::floor(m.pos.x), (int)std::floor(m.pos.y + 0.01f), (int)std::floor(m.pos.z)};
        int hb = (int)std::ceil(d.height);
        bool chasing = d.behavior == MobBehavior::Hostile && ctx.playerTargetable && distP < 16 && std::fabs(toP.y) < 8;
        if (chasing) {
            float reach = 1.0f + d.width * 0.5f;
            if (distP < reach && std::fabs(toP.y) < d.height) {
                if (m.attackCooldown == 0) { m.attackCooldown = 20; m.anim.play(AnimState::Attack); if (ctx.attackPlayer) ctx.attackPlayer(m, d.damage); }
                m.path.clear();
                want = normalize(Vec3(toP.x, 0, toP.z)) * 0.3f;
            } else if (--m.pathTimer <= 0) {
                m.pathTimer = 10 + rng.range(5);
                BlockPos goal{(int)std::floor(pp.x), (int)std::floor(pp.y + 0.01f), (int)std::floor(pp.z)};
                m.path = findPath(w, feet, goal, hb, 400); m.pathIdx = 1;
                if (m.path.empty()) want = normalize(Vec3(toP.x, 0, toP.z));   // no path: head straight at the player
            }
            speed *= 1.2f;
        } else if (m.panicTicks > 0) {
            m.panicTicks--;
            if (m.path.empty() || m.pathIdx >= m.path.size()) {
                Vec3 away = normalize(Vec3(-toP.x, 0, -toP.z) + Vec3(rng.unit() - 0.5f, 0, rng.unit() - 0.5f));
                BlockPos g{feet.x + (int)(away.x * 8), feet.y, feet.z + (int)(away.z * 8)};
                m.path = findPath(w, feet, g, hb, 200); m.pathIdx = 1;
            }
            speed *= 1.8f;
        } else if (m.path.empty() || m.pathIdx >= m.path.size()) {
            if (--m.idleTimer <= 0) {
                m.idleTimer = 60 + rng.range(140);
                if (rng.chance(0.6f)) {
                    BlockPos g{feet.x + rng.range(17) - 8, feet.y, feet.z + rng.range(17) - 8};
                    for (int dy = 3; dy >= -3; dy--) if (walkableCell(w, {g.x, g.y + dy, g.z}, hb)) { g.y += dy; break; }
                    m.path = findPath(w, feet, g, hb, 200); m.pathIdx = 1;
                }
            }
        }
        // ---- follow path
        if (!m.path.empty() && m.pathIdx < m.path.size()) {
            const BlockPos& n = m.path[m.pathIdx];
            Vec3 c{n.x + 0.5f, (float)n.y, n.z + 0.5f};
            Vec3 dv = c - m.pos; float hd = std::sqrt(dv.x * dv.x + dv.z * dv.z);
            if (hd < 0.35f && std::fabs(dv.y) < 1.1f) m.pathIdx++;
            else { want = Vec3(dv.x, 0, dv.z) * (1.0f / std::max(hd, 1e-4f)); if (n.y > feet.y && m.onGround) m.vel.y = 0.42f; }
        }
        if (want.x != 0 || want.z != 0) {
            m.vel.x += (want.x * speed - m.vel.x) * 0.5f; m.vel.z += (want.z * speed - m.vel.z) * 0.5f;
            float ty = std::atan2(want.x, -want.z);
            float dyaw = std::remainder(ty - m.yaw, 2 * PI);
            m.yaw += clamp(dyaw, -0.35f, 0.35f);
        }
        move(w, m, d);
        // head looks at the player when close
        if (distP < 8) {
            float ly = std::atan2(toP.x, -toP.z);
            m.headYaw = clamp((float)std::remainder(ly - m.yaw, 2 * PI), -1.0f, 1.0f);
            m.headPitch = std::atan2(toP.y + 1.0f - d.height * 0.8f, std::max(distP, 0.1f));
        } else { m.headYaw *= 0.9f; m.headPitch *= 0.9f; }
    }
    mobs.erase(std::remove_if(mobs.begin(), mobs.end(), [](const Mob& m) { return m.removed; }), mobs.end());
}

void MobSystem::spawnTick(const World& w, const Player& p, float daylight, Rng& rng, int passiveCap, int hostileCap, const std::string& dimension) {
    int passive = countNear(p.pos, 64, MobBehavior::Passive), hostile = countNear(p.pos, 64, MobBehavior::Hostile);
    for (int attempt = 0; attempt < 4; attempt++) {
        // weighted pick of a mob type
        auto allowed = [&](const MobDef& md) { return std::find(md.dimensions.begin(), md.dimensions.end(), dimension) != md.dimensions.end(); };
        int total = 0; for (size_t i = 0; i < reg_.size(); i++) if (allowed(reg_.def((u16)i))) total += reg_.def((u16)i).weight;
        if (!total) return;
        int r = rng.range(total); u16 type = 0;
        for (size_t i = 0; i < reg_.size(); i++) { if (!allowed(reg_.def((u16)i))) continue; r -= reg_.def((u16)i).weight; if (r < 0) { type = (u16)i; break; } }
        const MobDef& d = reg_.def(type);
        if (d.behavior == MobBehavior::Passive && passive >= passiveCap) continue;
        if (d.behavior == MobBehavior::Hostile && hostile >= hostileCap) continue;
        // random column 24..56 blocks away
        float ang = rng.unit() * 2 * PI, dist = 24 + rng.unit() * 32;
        int x = (int)std::floor(p.pos.x + std::cos(ang) * dist), z = (int)std::floor(p.pos.z + std::sin(ang) * dist);
        if (!w.isReady({x >> 4, z >> 4})) continue;
        int top = w.surfaceY(x, z);
        if (top < 1) continue;
        int y = -1, hb = (int)std::ceil(d.height);
        auto light = [&](int yy) { int s = (int)(w.skyLight({x, yy, z}) * daylight); return std::max(s, (int)w.blockLight({x, yy, z})); };
        if (d.where == SpawnWhere::Surface) {
            if (w.reg.def(w.getBlock(x, top, z)).name == "turf" && w.skyLight({x, top + 1, z}) >= 9 && daylight > 0.4f) y = top + 1;
        } else if (d.where == SpawnWhere::Dark) {
            if (light(top + 1) <= 7 && w.reg.solid(w.getBlock(x, top, z))) y = top + 1;
            else { int yy = 10 + rng.range(std::max(1, top - 10)); if (walkableCell(w, {x, yy, z}, hb) && light(yy) <= 7) y = yy; }
        } else if (d.where == SpawnWhere::Air) {
            int yy = 36 + rng.range(80);
            bool open = true;
            for (int k = -1; k <= 2 && open; k++) open = w.getBlock(x, yy + k, z) == AIR;
            if (open) { spawn(type, {x + 0.5f, (float)yy, z + 0.5f}, rng); return; }
            continue;
        } else {
            int yy = 6 + rng.range(44);
            if (yy < top - 4 && walkableCell(w, {x, yy, z}, hb) && w.skyLight({x, yy, z}) == 0 && w.blockLight({x, yy, z}) == 0) y = yy;
        }
        if (y < 0 || !walkableCell(w, {x, y, z}, hb)) continue;
        int n = d.groupMin + rng.range(d.groupMax - d.groupMin + 1);
        n = std::min(n, d.behavior == MobBehavior::Passive ? passiveCap - passive : hostileCap - hostile);   // never overshoot the cap
        for (int i = 0; i < n; i++) {
            BlockPos q{x + rng.range(5) - 2, y, z + rng.range(5) - 2};
            for (int dy = 1; dy >= -1; dy--) if (walkableCell(w, {q.x, q.y + dy, q.z}, hb)) { q.y += dy; spawn(type, {q.x + 0.5f, (float)q.y, q.z + 0.5f}, rng); break; }
        }
        return;
    }
}

Mob* MobSystem::raycast(Vec3 o, Vec3 dir, float reach, float* outDist) {
    Mob* best = nullptr; float bestT = reach;
    for (auto& m : mobs) {
        if (m.removed || !m.alive()) continue;
        AABB b = m.box(reg_.def(m.type));
        float t0 = 0, t1 = bestT;
        bool hit = true;
        for (int a = 0; a < 3 && hit; a++) {
            float inv = std::fabs(dir[a]) < 1e-9f ? 1e30f : 1.0f / dir[a];
            float ta = (b.min[a] - o[a]) * inv, tb = (b.max[a] - o[a]) * inv;
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta); t1 = std::min(t1, tb);
            if (t0 > t1) hit = false;
        }
        if (hit && t0 < bestT) { bestT = t0; best = &m; }
    }
    if (outDist) *outDist = bestT;
    return best;
}

} // namespace vx
