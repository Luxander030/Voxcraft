#include "game/animation.h"

namespace vx {

namespace {
constexpr float DEG = PI / 180.0f;
Vec3 vec3(const Json& a) { return {(float)a[0].asNumber(), (float)a[1].asNumber(), (float)a[2].asNumber()}; }
}

bool loadModel(const Json& j, const TexturePackNames* tex, Model& m, std::string* err) {
    m = Model{}; m.name = j["name"].asString();
    for (auto& b : j["bones"].arr) {
        Model::Bone bone; bone.name = b["name"].asString(); bone.pivot = vec3(b["pivot"]);
        std::string parent = b["parent"].asString();
        bone.parent = parent.empty() ? -1 : m.bone(parent);
        if (!parent.empty() && bone.parent < 0) { if (err) *err = m.name + ": unknown parent " + parent; return false; }
        m.bones.push_back(bone);
    }
    for (auto& b : j["boxes"].arr) {
        Model::Box box; box.bone = m.bone(b["bone"].asString());
        if (box.bone < 0) { if (err) *err = m.name + ": box on unknown bone"; return false; }
        box.min = vec3(b["min"]); box.size = vec3(b["size"]);
        int all = tex ? tex->layerOf(b["tex"].asString()) : 0, front = tex ? tex->layerOf(b["front"].asString()) : 0;
        if (all < 0 || front < 0) { if (err) *err = m.name + ": missing texture " + b["tex"].asString(); return false; }
        for (int f = 0; f < 6; f++) box.tex[f] = (u16)all;
        box.tex[4] = (u16)front;   // -Z = forward
        m.boxes.push_back(box);
    }
    if (m.bones.empty() || m.boxes.empty()) { if (err) *err = m.name + ": empty model"; return false; }
    return true;
}

bool loadClips(const Json& rig, const Model& model, std::unordered_map<std::string, Clip>& out, std::string* err) {
    out.clear();
    for (auto& [name, c] : rig.obj) {
        Clip clip; clip.length = (float)c["length"].asNumber(1); clip.loop = c["loop"].asBool(false);
        for (auto& [bone, keys] : c["tracks"].obj) {
            int b = model.bone(bone);
            if (b < 0) continue;   // rigs are shared; a model may lack some bones
            std::vector<Clip::Key> ks;
            for (auto& k : keys.arr) ks.push_back({(float)k[0].asNumber(), vec3(k[1]) * DEG});
            std::sort(ks.begin(), ks.end(), [](const Clip::Key& a, const Clip::Key& b) { return a.t < b.t; });
            if (ks.empty()) { if (err) *err = "clip " + name + ": empty track"; return false; }
            clip.tracks.push_back({b, std::move(ks)});
        }
        out[name] = std::move(clip);
    }
    return true;
}

void Clip::sample(float t, float w, Pose& pose) const {
    if (loop && length > 0) t = std::fmod(t, length);
    for (auto& [bone, keys] : tracks) {
        Vec3 r;
        if (t <= keys.front().t) r = keys.front().rot;
        else if (t >= keys.back().t) r = keys.back().rot;
        else for (size_t i = 1; i < keys.size(); i++) if (t <= keys[i].t) {
            float f = (t - keys[i - 1].t) / std::max(1e-6f, keys[i].t - keys[i - 1].t);
            r = keys[i - 1].rot + (keys[i].rot - keys[i - 1].rot) * f; break;
        }
        pose.rot[bone] += r * w;
    }
}

const Clip* Animator::clipFor(AnimState s) const {
    if (!clips_) return nullptr;
    const char* n = s == AnimState::Attack ? "attack" : s == AnimState::Hurt ? "hurt" : s == AnimState::Death ? "death" : nullptr;
    if (!n) return nullptr;
    auto it = clips_->find(n); return it == clips_->end() ? nullptr : &it->second;
}

void Animator::play(AnimState s) {
    if (state_ == AnimState::Death) return;   // terminal
    if (s == state_ && s != AnimState::Attack && s != AnimState::Hurt) return;
    prev_ = state_; state_ = s; stateTime_ = 0; fade_ = 0;
}

void Animator::update(float dt, const AnimInput& in) {
    if (!model_) return;
    stateTime_ += dt;
    fade_ = std::min(1.0f, fade_ + dt / 0.15f);   // 150 ms crossfade
    // one-shot clips return to locomotion when done
    if ((state_ == AnimState::Attack || state_ == AnimState::Hurt) && clipFor(state_) && stateTime_ >= clipFor(state_)->length) { prev_ = state_; state_ = AnimState::Idle; fade_ = 0; stateTime_ = 0; }
    if (state_ == AnimState::Idle || state_ == AnimState::Walk) {
        AnimState loco = in.speed > 0.01f ? AnimState::Walk : AnimState::Idle;
        if (loco != state_) { prev_ = state_; state_ = loco; fade_ = 0; stateTime_ = 0; }
    }
    pose.reset(model_->bones.size());
    // procedural locomotion layer: limb swing proportional to speed
    float targetAmp = clamp(in.speed * 6.0f, 0.0f, 0.9f);
    walkAmp_ += (targetAmp - walkAmp_) * std::min(1.0f, dt * 8);
    walkPhase_ += dt * (4.0f + in.speed * 40.0f);
    float sw = std::sin(walkPhase_) * walkAmp_;
    auto add = [&](const char* n, Vec3 r) { int b = model_->bone(n); if (b >= 0) pose.rot[b] += r; };
    add("leg_fl", {sw, 0, 0}); add("leg_br", {sw, 0, 0}); add("leg_fr", {-sw, 0, 0}); add("leg_bl", {-sw, 0, 0});
    add("leg_l", {sw, 0, 0}); add("leg_r", {-sw, 0, 0});
    add("arm_l", {-sw * 0.8f, 0, 0}); add("arm_r", {sw * 0.8f, 0, 0});
    add("tail", {0, std::sin(walkPhase_ * 0.5f) * 0.3f, 0});
    orbit_ += dt * 2.2f;   // free-floating parts (cinderwisp shards) orbit the body
    add("shard_a", {0, orbit_, 0}); add("shard_b", {0, orbit_ * 1.3f, 0.3f}); add("shard_c", {0.4f, -orbit_ * 0.8f, 0});
    flap_ += dt * (in.onGround ? 2.0f : 25.0f);
    float fl = in.onGround ? std::sin(flap_) * 0.05f : std::sin(flap_) * 0.9f;
    add("wing_l", {0, 0, -fl}); add("wing_r", {0, 0, fl});
    // idle breathing
    add("body", {std::sin(stateTime_ * 2.0f) * 0.015f, 0, 0});
    // head look
    add("head", {clamp(-in.headPitch, -0.8f, 0.8f), clamp(-in.headYaw, -1.0f, 1.0f), 0});
    // keyframed clips, crossfaded
    if (const Clip* c = clipFor(state_)) c->sample(stateTime_, fade_, pose);
    if (fade_ < 1) if (const Clip* c = clipFor(prev_)) c->sample(c->length, 1 - fade_, pose);
}

void Animator::boneMatrices(std::vector<Mat4>& out) const {
    out.resize(model_->bones.size());
    for (size_t i = 0; i < model_->bones.size(); i++) {
        const Model::Bone& b = model_->bones[i];
        Vec3 r = i < pose.rot.size() ? pose.rot[i] : Vec3();
        Mat4 local = Mat4::translate(b.pivot.x, b.pivot.y, b.pivot.z) * Mat4::rotateZ(r.z) * Mat4::rotateY(r.y) * Mat4::rotateX(r.x) *
                     Mat4::translate(-b.pivot.x, -b.pivot.y, -b.pivot.z);
        out[i] = b.parent >= 0 ? out[b.parent] * local : local;
    }
}

} // namespace vx
