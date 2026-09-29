// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Voices.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace asma::audio;

namespace {

constexpr int kRate = 10000; // attack 20 frames, release 800

// 1, 2, 3, ... scaled small; linear, so pitched playback is exact.
std::shared_ptr<AudioBuffer> counting(int frames)
{
    auto b = std::make_shared<AudioBuffer>();
    b->sampleRate = kRate;
    std::vector<float> ch(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i) ch[static_cast<std::size_t>(i)] = static_cast<float>(i + 1) / 1000.0f;
    b->channels.push_back(std::move(ch));
    return b;
}

struct Pool {
    VoicePool pool;
    MemorySource src{counting(5000)};
    Pool() { pool.prepare(kRate, 256); }
    void on(int note, float velocity = 1.0f, std::uint64_t generation = 1, int root = VoicePool::kDefaultRootNote)
    {
        pool.noteOn(note, velocity, src, generation, {}, root);
    }
    std::vector<float> render(int n, float prefill = 0.0f)
    {
        std::vector<float> l(static_cast<std::size_t>(n), prefill), r(static_cast<std::size_t>(n), prefill);
        float* out[] = {l.data(), r.data()};
        pool.render(out, n);
        return l;
    }
};

} // namespace

TEST_CASE("VoicePool plays the root note at the sample's own pitch", "[voices]")
{
    Pool p;
    p.on(60);
    const auto out = p.render(600);
    CHECK(out[0] == Catch::Approx(0.001f * 0.05f)); // attack: 1/20 of the way up
    CHECK(out[100] == Catch::Approx(0.101f));        // held: the sample itself
    CHECK(out[500] == Catch::Approx(0.501f));
}

TEST_CASE("VoicePool pitches by speed, from the sample's root note", "[voices]")
{
    Pool p;
    p.on(72); // an octave up: twice as fast
    auto out = p.render(3000);
    CHECK(out[100] == Catch::Approx(0.201f));
    CHECK(out[2499] != 0.0f);
    CHECK(out[2500] == 0.0f); // 5000 frames played in 2500
    CHECK(p.pool.active() == 0);

    p.on(69, 1.0f, 1, 57); // smpl chunk says A3: A4 is an octave up
    out = p.render(200);
    CHECK(out[100] == Catch::Approx(0.201f));
}

TEST_CASE("VoicePool scales by velocity and mixes into the output", "[voices]")
{
    Pool p;
    p.on(60, 0.5f);
    const auto out = p.render(200, 1.0f);
    CHECK(out[100] == Catch::Approx(1.0f + 0.5f * 0.101f));
}

TEST_CASE("VoicePool releases on note-off", "[voices]")
{
    Pool p;
    p.on(60);
    p.render(100);
    p.pool.noteOff(60);
    CHECK(p.pool.active() == 1); // still releasing
    const auto out = p.render(1000);
    CHECK(out[0] == Catch::Approx(0.101f * (1.0f - 1.0f / 800)));
    CHECK(out[400] == Catch::Approx(0.501f * (1.0f - 401.0f / 800)).margin(1e-4)); // 401 float steps
    CHECK(out[800] == 0.0f);
    CHECK(p.pool.active() == 0);
}

TEST_CASE("VoicePool steals a releasing voice first, then the oldest", "[voices]")
{
    Pool p;
    for (int n = 0; n < VoicePool::kVoices; ++n) p.on(60 + n, 1.0f, static_cast<std::uint64_t>(n + 1));
    CHECK(p.pool.active() == 8);
    CHECK(p.pool.oldestGeneration() == 1);

    p.on(80, 1.0f, 9); // takes the oldest: note 60, generation 1
    CHECK(p.pool.active() == 8);
    CHECK(p.pool.oldestGeneration() == 2);

    p.pool.noteOff(65); // generation 6, releasing
    p.on(81, 1.0f, 10); // takes 65 rather than the oldest (61)
    CHECK(p.pool.oldestGeneration() == 2);
    p.pool.noteOff(61);
    p.render(1000); // 61 fades out
    CHECK(p.pool.oldestGeneration() == 3);
}

TEST_CASE("VoicePool restarts a note that is already sounding", "[voices]")
{
    Pool p;
    p.on(60);
    p.render(50);
    p.on(60);
    CHECK(p.pool.active() == 2); // the first one is on its way out
    p.render(1000);
    CHECK(p.pool.active() == 1);
    p.pool.kill();
    CHECK(p.pool.active() == 0);
    CHECK(p.pool.oldestGeneration() == VoicePool::kNone);
}
