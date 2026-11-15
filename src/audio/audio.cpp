#include "audio/audio.h"

namespace vx {

namespace {
constexpr float SR = (float)Mixer::RATE;
struct Gen {
    Rng rng;
    std::vector<float> s;
    explicit Gen(const std::string& name, float seconds) : rng(fnv1a(name)), s((size_t)(seconds * SR), 0.0f) {}
    float noise() { return rng.unit() * 2 - 1; }
    // Filtered noise burst: one-pole lowpass at `cut` Hz minus a lowpass at `low` Hz (= band), exp decay.
    void burst(float start, float len, float cut, float low, float amp, float decay) {
        float a1 = std::exp(-2 * PI * cut / SR), a2 = std::exp(-2 * PI * low / SR), y1 = 0, y2 = 0;
        size_t s0 = (size_t)(start * SR), n = (size_t)(len * SR);
        for (size_t i = 0; i < n && s0 + i < s.size(); i++) {
            float x = noise();
            y1 = (1 - a1) * x + a1 * y1; y2 = (1 - a2) * x + a2 * y2;
            float t = i / SR, env = std::min(1.0f, t / 0.002f) * std::exp(-t * decay);
            s[s0 + i] += (y1 - y2) * amp * env;
        }
    }
    // Damped resonator excited by a click (wood/glass/stone body).
    void ring(float start, float freq, float amp, float decay) {
        size_t s0 = (size_t)(start * SR);
        for (size_t i = 0; s0 + i < s.size(); i++) {
            float t = i / SR, e = std::exp(-t * decay);
            if (e < 1e-3f) break;
            s[s0 + i] += std::sin(2 * PI * freq * t) * amp * e;
        }
    }
    // Frequency/amplitude modulated tone (creature calls, UI).
    void fm(float start, float len, float f0, float f1, float modRatio, float modIndex, float amp, float vibrato = 0) {
        size_t s0 = (size_t)(start * SR), n = (size_t)(len * SR);
        double ph = 0, mph = 0;
        for (size_t i = 0; i < n && s0 + i < s.size(); i++) {
            float t = i / SR, u = t / len;
            float f = f0 + (f1 - f0) * u + std::sin(t * 2 * PI * 6) * vibrato;
            mph += 2 * PI * f * modRatio / SR; ph += 2 * PI * f / SR;
            float env = std::min(1.0f, t / 0.01f) * std::min(1.0f, (len - t) / 0.05f);
            s[s0 + i] += (float)std::sin(ph + std::sin(mph) * modIndex) * amp * std::max(0.0f, env);
        }
    }
    SoundBuffer done() {
        float peak = 1e-6f; for (float v : s) peak = std::max(peak, std::fabs(v));
        float g = 0.9f / peak;
        SoundBuffer b; b.pcm.resize(s.size());
        for (size_t i = 0; i < s.size(); i++) b.pcm[i] = (i16)std::lround(clamp(s[i] * g, -1.0f, 1.0f) * 32000.0f);
        return b;
    }
};

struct Material { const char* name; float cut, low, decay; float ring, ringAmp, ringDecay; float grains; };
const Material MATS[] = {
    {"stone", 3200, 500, 40, 900, 0.15f, 60, 2},
    {"wood", 1800, 120, 30, 210, 0.6f, 25, 1},
    {"grass", 6000, 1500, 25, 0, 0, 0, 5},
    {"gravel", 4000, 600, 35, 0, 0, 0, 7},
    {"sand", 7000, 2500, 18, 0, 0, 0, 4},
    {"snow", 1500, 200, 22, 0, 0, 0, 3},
    {"glass", 8000, 3000, 50, 2600, 0.5f, 18, 1},
    {"metal", 5000, 800, 30, 1250, 0.7f, 14, 1},
    {"wool", 1200, 100, 30, 0, 0, 0, 2},
};
} // namespace

void SoundBank::generate() {
    sounds_.clear();
    for (const Material& m : MATS) {
        std::string n = m.name;
        { Gen g("step_" + n, 0.18f); for (int k = 0; k < (int)m.grains; k++) g.burst(k * 0.012f, 0.12f, m.cut * 0.7f, m.low, 0.6f, m.decay); if (m.ring) g.ring(0, m.ring * 0.8f, m.ringAmp * 0.5f, m.ringDecay * 1.5f); sounds_["step_" + n] = g.done(); }
        { Gen g("dig_" + n, 0.22f); for (int k = 0; k < (int)m.grains + 1; k++) g.burst(k * 0.015f, 0.15f, m.cut, m.low, 0.8f, m.decay * 0.8f); if (m.ring) g.ring(0, m.ring, m.ringAmp, m.ringDecay); sounds_["dig_" + n] = g.done(); }
        { Gen g("place_" + n, 0.25f); g.burst(0, 0.2f, m.cut * 0.5f, m.low * 0.5f, 1.0f, m.decay * 0.7f); if (m.ring) g.ring(0, m.ring * 0.7f, m.ringAmp, m.ringDecay); sounds_["place_" + n] = g.done(); }
        { Gen g("break_" + n, 0.45f); for (int k = 0; k < (int)m.grains + 4; k++) g.burst(k * 0.03f, 0.25f, m.cut, m.low, 0.9f - k * 0.06f, m.decay * 0.6f);
          if (m.ring) { g.ring(0, m.ring, m.ringAmp, m.ringDecay * 0.8f); g.ring(0.02f, m.ring * 1.51f, m.ringAmp * 0.6f, m.ringDecay); } sounds_["break_" + n] = g.done(); }
    }
    { Gen g("ui_click", 0.06f); g.fm(0, 0.05f, 1400, 1100, 2, 0.8f, 0.6f); sounds_["ui_click"] = g.done(); }
    { Gen g("pickup", 0.12f); g.fm(0, 0.1f, 700, 1500, 1, 0.3f, 0.6f); sounds_["pickup"] = g.done(); }
    { Gen g("hurt", 0.25f); g.fm(0, 0.2f, 220, 120, 1.5f, 2.0f, 0.8f); g.burst(0, 0.1f, 1500, 100, 0.4f, 30); sounds_["hurt"] = g.done(); }
    { Gen g("eat", 0.5f); for (int k = 0; k < 5; k++) g.burst(k * 0.09f, 0.07f, 3500, 400, 0.8f, 40); sounds_["eat"] = g.done(); }
    { Gen g("splash", 0.6f); g.burst(0, 0.55f, 5000, 300, 1.0f, 6); g.burst(0.05f, 0.3f, 1500, 200, 0.5f, 12); sounds_["splash"] = g.done(); }
    { Gen g("click_circuit", 0.08f); g.burst(0, 0.05f, 6000, 1500, 1.0f, 120); g.ring(0, 1800, 0.3f, 90); sounds_["click_circuit"] = g.done(); }
    { Gen g("piston", 0.35f); g.burst(0, 0.3f, 900, 60, 1.0f, 12); g.ring(0, 140, 0.6f, 14); sounds_["piston"] = g.done(); }
    // creatures
    { Gen g("grazer_call", 0.7f); g.fm(0, 0.6f, 180, 150, 1.0f, 1.5f, 0.8f, 8); sounds_["grazer_call"] = g.done(); }
    { Gen g("puffbird_call", 0.4f); g.fm(0, 0.08f, 2200, 2900, 1, 0.5f, 0.7f); g.fm(0.14f, 0.1f, 2400, 3300, 1, 0.5f, 0.7f); sounds_["puffbird_call"] = g.done(); }
    { Gen g("gloomling_call", 1.0f); g.fm(0, 0.9f, 95, 70, 0.51f, 3.0f, 0.8f, 6); g.burst(0, 0.9f, 400, 40, 0.3f, 2); sounds_["gloomling_call"] = g.done(); }
    { Gen g("rockmite_call", 0.3f); for (int k = 0; k < 4; k++) g.burst(k * 0.06f, 0.03f, 7000, 2500, 0.8f, 150); sounds_["rockmite_call"] = g.done(); }
    { Gen g("mob_hurt", 0.25f); g.fm(0, 0.2f, 400, 250, 1.2f, 2.0f, 0.8f); sounds_["mob_hurt"] = g.done(); }
    { Gen g("mob_death", 0.6f); g.fm(0, 0.55f, 300, 80, 1.3f, 2.5f, 0.8f); g.burst(0.05f, 0.4f, 2000, 100, 0.3f, 6); sounds_["mob_death"] = g.done(); }
    // ambient
    { Gen g("cave_drone", 4.0f); g.fm(0, 4.0f, 55, 52, 0.5f, 0.8f, 0.5f, 0.5f); g.burst(0, 4.0f, 300, 30, 0.3f, 0.2f); sounds_["cave_drone"] = g.done(); }
    { Gen g("drip", 0.3f); g.fm(0, 0.12f, 1600, 2600, 1, 0.2f, 0.8f); sounds_["drip"] = g.done(); }
}

size_t SoundBank::bytes() const { size_t b = 0; for (auto& [k, v] : sounds_) b += v.pcm.size() * 2; return b; }

void Mixer::play(const std::string& s, SoundCat c, float gain, float pitch) {
    std::lock_guard<std::mutex> lk(m_); pending_.push_back({s, c, gain, pitch, 16, false, {}});
}
void Mixer::play3d(const std::string& s, SoundCat c, Vec3 p, float gain, float pitch, float range) {
    std::lock_guard<std::mutex> lk(m_); pending_.push_back({s, c, gain, pitch, range, true, p});
}
void Mixer::setListener(Vec3 p, float yaw) { std::lock_guard<std::mutex> lk(m_); listener_ = p; listenerYaw_ = yaw; }
void Mixer::startMusic(u64 seed) { musicSeed_.store(seed); musicStart_.store(true); }
void Mixer::stopAll() { std::lock_guard<std::mutex> lk(m_); pending_.clear(); for (auto& v : voices_) v.on = false; musicOn_ = false; }

void Mixer::applyCommands() {
    std::vector<Cmd> cmds;
    { std::unique_lock<std::mutex> lk(m_, std::try_to_lock); if (!lk.owns_lock()) return; cmds.swap(pending_); }   // never block the audio thread
    for (auto& c : cmds) {
        const SoundBuffer* b = bank_.get(c.sound);
        if (!b) continue;
        Voice* slot = nullptr;
        for (auto& v : voices_) if (!v.on) { slot = &v; break; }
        if (!slot) {   // steal the voice closest to finishing
            double best = -1;
            for (auto& v : voices_) { double rem = v.pos / std::max<size_t>(1, v.buf->pcm.size()); if (rem > best) { best = rem; slot = &v; } }
        }
        *slot = Voice{b, 0, c.pitch, c.gain, c.cat, c.spatial, c.at, c.range, true};
    }
}

void Mixer::musicSample(float& l, float& r) {
    // Generative ambient music: slow pentatonic notes over a soft fifth drone. Original by construction.
    if (musicStart_.exchange(false)) { musicOn_ = true; musicRng_ = musicSeed_.load() | 1; musicT_ = 0; noteEnd_ = 0; pieceEnd_ = 75 + (double)(musicRng_ % 30); }
    if (!musicOn_) return;
    static const float scale[] = {0, 2, 4, 7, 9, 12, 14, 16};
    double dt = 1.0 / RATE;
    musicT_ += dt;
    if (musicT_ >= noteEnd_) {
        musicRng_ ^= musicRng_ << 13; musicRng_ ^= musicRng_ >> 7; musicRng_ ^= musicRng_ << 17;
        float root = 220.0f * std::pow(2.0f, (float)(musicRng_ % 3) / 12.0f);
        notes_[0] = root * std::pow(2.0f, scale[(musicRng_ >> 8) % 8] / 12.0f);
        notes_[1] = root * 0.5f; notes_[2] = root * 0.75f;
        noteEnd_ = musicT_ + 1.6 + (double)((musicRng_ >> 16) % 4) * 0.4;
    }
    double tn = noteEnd_ - musicT_;
    float env = (float)std::min(1.0, std::min((musicT_ - (noteEnd_ - 2.0)) / 0.3 + 1.0, tn / 0.8));
    noteAmp_ += (std::max(0.0f, env) - noteAmp_) * 0.0005f;
    float fade = (float)std::min(1.0, std::min(musicT_ / 4.0, (pieceEnd_ - musicT_) / 6.0));
    if (musicT_ > pieceEnd_) { musicOn_ = false; return; }
    float v = 0;
    for (int i = 0; i < 3; i++) { phase_[i] += 2 * PI * notes_[i] * dt; if (phase_[i] > 2 * PI) phase_[i] -= 2 * PI; }
    v += (float)std::sin(phase_[0]) * 0.18f * noteAmp_;
    v += (float)(std::sin(phase_[1]) + 0.5 * std::sin(phase_[2])) * 0.05f;
    v *= fade * catGain(SoundCat::Music);
    l += v; r += v;
}

void Mixer::render(float* out, int frames) {
    applyCommands();
    Vec3 lp; float ly;
    { std::unique_lock<std::mutex> lk(m_, std::try_to_lock); lp = listener_; ly = listenerYaw_; }
    // per-voice constant gains for this block
    float gl[MAX_VOICES], gr[MAX_VOICES];
    int act = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
        Voice& v = voices_[i];
        if (!v.on) { gl[i] = gr[i] = 0; continue; }
        act++;
        float g = v.gain * catGain(v.cat), pan = 0;
        if (v.spatial) {
            Vec3 d = v.at - lp; float dist = length(d);
            g *= clamp(1.0f - dist / v.range, 0.0f, 1.0f);
            // listener right vector for yaw (yaw 0 looks -Z, right = +X)
            Vec3 right{std::cos(ly), 0, std::sin(ly)};
            pan = dist > 0.5f ? clamp(dot(d, right) / dist, -1.0f, 1.0f) * 0.8f : 0;
        }
        float a = (pan + 1) * PI / 4;   // equal-power pan
        gl[i] = g * std::cos(a); gr[i] = g * std::sin(a);
    }
    active_.store(act);
    for (int f = 0; f < frames; f++) {
        float l = 0, r = 0;
        for (int i = 0; i < MAX_VOICES; i++) {
            Voice& v = voices_[i];
            if (!v.on) continue;
            size_t k = (size_t)v.pos;
            if (k + 1 >= v.buf->pcm.size()) { v.on = false; continue; }
            float frac = (float)(v.pos - k);
            float s = (v.buf->pcm[k] * (1 - frac) + v.buf->pcm[k + 1] * frac) * (1.0f / 32768.0f);
            l += s * gl[i]; r += s * gr[i];
            v.pos += v.step;
        }
        musicSample(l, r);
        out[f * 2] = std::tanh(l); out[f * 2 + 1] = std::tanh(r);   // soft clip
    }
}

} // namespace vx
