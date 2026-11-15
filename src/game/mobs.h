// Creatures: data-driven definitions (assets/data/mobs.json), physics, AI, pathfinding, spawning.
//
// AI is tick-based and cheap: paths are recomputed at most every 10 ticks with a node budget,
// mobs far outside the simulation distance are frozen, and hostile mobs far from the player
// despawn. Passive mobs persist with their chunk (see EntityManager).
#pragma once
#include "game/animation.h"
#include "game/items.h"
#include "game/player.h"

namespace vx {

enum class MobBehavior : u8 { Passive, Hostile };
enum class SpawnWhere : u8 { Surface, Dark, Underground, Air };

struct MobDef {
    std::string name, rig;
    u16 id = 0;
    int model = -1;
    float health = 10, speed = 0.1f, width = 0.6f, height = 1.8f, damage = 0;
    MobBehavior behavior = MobBehavior::Passive;
    bool burnsInDaylight = false, flying = false, ranged = false, emissive = false, trader = false;
    std::vector<std::string> dimensions{"overworld"};
    struct Drop { u16 item; int min, max; };
    std::vector<Drop> drops;
    SpawnWhere where = SpawnWhere::Surface;
    int groupMin = 1, groupMax = 1, weight = 1;
};

class MobRegistry {
public:
    bool load(const Json& mobs, const Json& models, const Json& anims, const ItemRegistry& items, const TexturePackNames* tex, std::string* err);
    const MobDef& def(u16 id) const { return defs_[id]; }
    u16 id(const std::string& n) const { for (auto& d : defs_) if (d.name == n) return d.id; return 0xFFFF; }
    size_t size() const { return defs_.size(); }
    const Model& model(int i) const { return models_[i]; }
    const std::unordered_map<std::string, Clip>& clips(u16 id) const { return clips_[id]; }
private:
    std::vector<MobDef> defs_;
    std::vector<Model> models_;
    std::vector<std::unordered_map<std::string, Clip>> clips_;
};

struct Mob {
    u16 type = 0;
    Vec3 pos, vel, prevPos;
    float yaw = 0, prevYaw = 0, headYaw = 0, headPitch = 0, health = 10;
    bool onGround = false, removed = false;
    int hurtTicks = 0, deathTicks = -1, attackCooldown = 0, panicTicks = 0, age = 0, pathTimer = 0, idleTimer = 0, burnTimer = 0;
    std::vector<BlockPos> path; size_t pathIdx = 0;
    Vec3 home; bool hasHome = false;   // settlers stay near their village
    u32 seed = 0;                      // per-creature variation (trade offers)
    Animator anim;
    AABB box(const MobDef& d) const { float h = d.width * 0.5f; return {{pos.x - h, pos.y, pos.z - h}, {pos.x + h, pos.y + d.height, pos.z + h}}; }
    bool alive() const { return deathTicks < 0; }
};

// A* over walkable block cells (feet positions). Deterministic; returns empty if no path within budget.
std::vector<BlockPos> findPath(const World& w, BlockPos start, BlockPos goal, int heightBlocks, int maxNodes = 600);
bool walkableCell(const World& w, const BlockPos& p, int heightBlocks);

struct MobContext {
    const World& world;
    const Player& player;
    bool playerTargetable;          // survival and alive
    float daylight;                 // 0..1
    std::function<void(Mob&, float dmg)> attackPlayer;
    std::function<void(const Mob&, const std::vector<ItemStack>&)> dropLoot;
    std::function<void(Mob&, Vec3 from, Vec3 vel)> shoot;   // ranged attackers
    std::string dimension = "overworld";
};

class MobSystem {
public:
    explicit MobSystem(const MobRegistry& reg) : reg_(reg) {}
    std::vector<Mob> mobs;
    Mob& spawn(u16 type, Vec3 pos, Rng& rng);
    void tick(MobContext& ctx, Rng& rng);
    void spawnTick(const World& w, const Player& p, float daylight, Rng& rng, int passiveCap = 10, int hostileCap = 12, const std::string& dimension = "overworld");
    // Player melee. Returns true if the mob took damage.
    bool hurt(Mob& m, float dmg, Vec3 knockDir, Rng& rng, MobContext* ctx);
    Mob* raycast(Vec3 origin, Vec3 dir, float reach, float* dist = nullptr);
    const MobRegistry& registry() const { return reg_; }
    int countNear(Vec3 p, float radius, MobBehavior b) const;
private:
    void move(const World& w, Mob& m, const MobDef& d);
    void killDrops(Mob& m, Rng& rng, MobContext* ctx);
    const MobRegistry& reg_;
};

} // namespace vx
