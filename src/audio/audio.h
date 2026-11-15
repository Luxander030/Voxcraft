// Audio: procedural sound synthesis + software mixer. Platform output (sokol_audio) only pulls
// mixed frames; everything here is platform independent and runs headless (tests).
//
// All sounds are original and generated at startup from small DSP recipes (filtered noise,
// resonators, FM), so there are no audio files on disk and the bank costs ~1-2 MiB of RAM
// (16-bit mono). Mixing: up to 32 voices, distance attenuation + equal-power stereo panning
// relative to the listener, six volume categories, plus a generative ambient music voice.
#pragma once
#include "core/common.h"
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vx {

enum class SoundCat : u8 { Master = 0, Music, Blocks, Creatures, Ambient, Ui, Count };

struct SoundBuffer { std::vector<i16> pcm; };   // mono, Mixer::RATE

class SoundBank {
public:
    void generate();   // deterministic per platform (libm sin/exp may differ in the last bits across platforms)
    const SoundBuffer* get(const std::string& name) const { auto it = sounds_.find(name); return it == sounds_.end() ? nullptr : &it->second; }
    size_t bytes() const;
    size_t count() const { return sounds_.size(); }
private:
    std::unordered_map<std::string, SoundBuffer> sounds_;
};

class Mixer {
public:
    static constexpr int RATE = 44100, MAX_VOICES = 32;
    explicit Mixer(const SoundBank& bank) : bank_(bank) { for (auto& v : vol_) v.store(1.0f); }
    // Thread-safe requests (main thread).
    void play(const std::string& sound, SoundCat cat, float gain = 1.0f, float pitch = 1.0f);
    void play3d(const std::string& sound, SoundCat cat, Vec3 pos, float gain = 1.0f, float pitch = 1.0f, float range = 16.0f);
    void setListener(Vec3 pos, float yaw);
    void setVolume(SoundCat c, float v) { vol_[(int)c].store(v); }
    void startMusic(u64 seed);
    void stopAll();
    // Audio thread: mix `frames` stereo frames (interleaved) into out (overwrites).
    void render(float* out, int frames);
    int activeVoices() const { return active_.load(); }
private:
    struct Voice { const SoundBuffer* buf = nullptr; double pos = 0, step = 1; float gain = 1; SoundCat cat = SoundCat::Blocks; bool spatial = false; Vec3 at; float range = 16; bool on = false; };
    struct Cmd { std::string sound; SoundCat cat; float gain, pitch, range; bool spatial; Vec3 at; };
    void applyCommands();
    float catGain(SoundCat c) const { return vol_[(int)SoundCat::Master].load() * vol_[(int)c].load(); }
    void musicSample(float& l, float& r);
    const SoundBank& bank_;
    std::mutex m_;
    std::vector<Cmd> pending_;
    Vec3 listener_; float listenerYaw_ = 0;
    std::atomic<float> vol_[(int)SoundCat::Count];
    Voice voices_[MAX_VOICES];
    std::atomic<int> active_{0};
    // generative music state (audio thread only after start)
    std::atomic<u64> musicSeed_{0}; std::atomic<bool> musicStart_{false};
    bool musicOn_ = false; u64 musicRng_ = 0; double musicT_ = 0, noteEnd_ = 0, pieceEnd_ = 0; float notes_[3] = {}; double phase_[3] = {}; float noteAmp_ = 0;
public:
    Mixer(const Mixer&) = delete;
};

} // namespace vx
