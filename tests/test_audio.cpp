// Procedural sound bank and mixer (headless).
#include "testing.h"
#include "audio/audio.h"

using namespace vx;

namespace { const SoundBank& bank() { static SoundBank b; static bool g = (b.generate(), true); (void)g; return b; } }

TEST(audio_bank_generates_original_sounds_cheaply) {
    CHECK(bank().count() >= 40);
    CHECK(bank().bytes() < 4u << 20);   // < 4 MiB for the whole bank
    for (const char* n : {"step_stone", "break_wood", "place_glass", "ui_click", "grazer_call", "splash"}) {
        const SoundBuffer* s = bank().get(n);
        REQUIRE(s);
        int peak = 0; for (i16 v : s->pcm) peak = std::max(peak, std::abs((int)v));
        CHECK(peak > 20000);   // normalised, not silent
    }
    SoundBank again; again.generate();
    CHECK(again.get("break_stone")->pcm == bank().get("break_stone")->pcm);   // deterministic
}

TEST(audio_mixer_volume_pan_range_and_voice_lifetime) {
    Mixer m(bank());
    std::vector<float> out(2 * 1024);
    m.setListener({0, 0, 0}, 0);   // facing -Z: right is +X
    m.play3d("dig_stone", SoundCat::Blocks, {6, 0, 0});
    m.render(out.data(), 1024);
    double l = 0, r = 0; for (int i = 0; i < 1024; i++) { l += std::fabs(out[i * 2]); r += std::fabs(out[i * 2 + 1]); }
    CHECK(r > l * 2);                      // source on the right
    CHECK(m.activeVoices() == 1);
    for (int i = 0; i < 40; i++) m.render(out.data(), 1024);
    CHECK(m.activeVoices() == 0);          // finished voices are freed
    m.play3d("dig_stone", SoundCat::Blocks, {100, 0, 0});   // out of range
    m.render(out.data(), 1024);
    double e = 0; for (float v : out) e += std::fabs(v); CHECK(e == 0);
    m.setVolume(SoundCat::Blocks, 0);
    m.play("dig_stone", SoundCat::Blocks);
    m.render(out.data(), 1024);
    e = 0; for (float v : out) e += std::fabs(v); CHECK(e == 0);   // category muted
    m.play("ui_click", SoundCat::Ui);
    m.render(out.data(), 1024);
    e = 0; for (float v : out) e += std::fabs(v); CHECK(e > 0);
}
