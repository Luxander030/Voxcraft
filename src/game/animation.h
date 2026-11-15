// Box models and the animation system.
//
// Model: bones (name, parent, pivot) + textured boxes attached to bones, loaded from the JSON
// exported from OpenSCAD sources (tools/scad_export.py). Units are pixels (1/16 block).
//
// Animation: a Pose holds a local rotation per bone (radians, applied X then Y then Z around the
// bone's pivot). Keyframed Clips are sampled with linear interpolation; the Animator state machine
// crossfades between clip states (idle / walk / attack / hurt / death) and adds procedural layers
// (walk cycle scaled by speed, wing flap, head look). World matrices are computed parent-first.
// Distant entities can be updated at a reduced rate by the caller (see EntityRenderer).
#pragma once
#include "core/common.h"
#include "core/json.h"
#include "game/items.h"
#include <unordered_map>

namespace vx {

struct Model {
    struct Bone { std::string name; int parent = -1; Vec3 pivot; };
    struct Box { int bone = 0; Vec3 min, size; u16 tex[6] = {}; };   // faces NX PX NY PY NZ PZ
    std::string name;
    std::vector<Bone> bones;
    std::vector<Box> boxes;
    int bone(const std::string& n) const { for (size_t i = 0; i < bones.size(); i++) if (bones[i].name == n) return (int)i; return -1; }
};
bool loadModel(const Json& j, const TexturePackNames* tex, Model& out, std::string* err);

struct Pose {
    std::vector<Vec3> rot;   // per bone, radians
    void reset(size_t n) { rot.assign(n, Vec3()); }
};

struct Clip {
    float length = 1; bool loop = false;
    struct Key { float t; Vec3 rot; };
    std::vector<std::pair<int, std::vector<Key>>> tracks;   // bone index -> keys (sorted)
    // Adds weight * sampled rotation into pose.
    void sample(float t, float weight, Pose& pose) const;
};
// Loads the clips for one rig, resolving bone names against `model`.
bool loadClips(const Json& rig, const Model& model, std::unordered_map<std::string, Clip>& out, std::string* err);

enum class AnimState : u8 { Idle, Walk, Attack, Hurt, Death };

struct AnimInput {
    float speed = 0;          // horizontal blocks per tick
    bool onGround = true;
    float headYaw = 0, headPitch = 0;   // relative to body, radians; pitch > 0 looks up
    // Player-only stances (creatures leave them off).
    bool sneaking = false, swimming = false, climbing = false;
    float holdArm = 0;        // 0..1: right arm raised forward (using an item, drawing a bow, eating)
};

class Animator {
public:
    void bind(const Model* m, const std::unordered_map<std::string, Clip>* clips) { model_ = m; clips_ = clips; pose.reset(m->bones.size()); }
    void play(AnimState s);   // attack / hurt / death are one-shot and return to locomotion
    void update(float dt, const AnimInput& in);
    AnimState state() const { return state_; }
    Pose pose;
    // Bone world matrices (model space, pixels), parent-first.
    void boneMatrices(std::vector<Mat4>& out) const;
private:
    const Model* model_ = nullptr;
    const std::unordered_map<std::string, Clip>* clips_ = nullptr;
    AnimState state_ = AnimState::Idle, prev_ = AnimState::Idle;
    float stateTime_ = 0, fade_ = 1, walkPhase_ = 0, walkAmp_ = 0, flap_ = 0, orbit_ = 0;
    float sneak_ = 0, swim_ = 0, climb_ = 0, air_ = 0, hold_ = 0;
    const Clip* clipFor(AnimState s) const;
};

} // namespace vx
