// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/audio/Stretcher.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

using namespace asma::audio;

namespace {

constexpr int kRate = 44100;
constexpr int kBlock = 512;

std::shared_ptr<AudioBuffer> mono(std::vector<float> samples)
{
    auto b = std::make_shared<AudioBuffer>();
    b->sampleRate = kRate;
    b->channels.push_back(std::move(samples));
    return b;
}

// Plays the whole sound through the stretcher in blocks; returns the left channel.
std::vector<float> play(Stretcher& st, SampleSource& src, double ratio, double semitones, bool allowBypass,
                        int maxFrames)
{
    PlayHead head;
    head.prepare(kRate);
    head.start(src, {});
    st.setTiming(ratio, semitones);
    st.start(head, allowBypass);
    std::vector<float> left, l(kBlock), r(kBlock);
    float* out[] = {l.data(), r.data()};
    while (st.active(head) && static_cast<int>(left.size()) < maxFrames) {
        st.process(head, out, kBlock);
        left.insert(left.end(), l.begin(), l.end());
    }
    return left;
}

// Frequency from zero crossings over [from, to).
double frequency(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    int crossings = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if ((x[i - 1] < 0.0f) != (x[i] < 0.0f)) ++crossings;
    return crossings / 2.0 / (static_cast<double>(to - from) / kRate);
}

// Seconds until the sound falls below 0.05 (-26 dB against 0.5) for good:
// where it ends, not where the stretch's decay dies out.
double soundingSeconds(const std::vector<float>& x)
{
    std::size_t last = 0;
    for (std::size_t i = 0; i < x.size(); ++i)
        if (std::abs(x[i]) > 0.05f) last = i;
    return static_cast<double>(last) / kRate;
}

} // namespace

TEST_CASE("Stretcher at neutral timing passes the sound through untouched", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 0.3, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 1.0, 0.0, true, kRate);
    CHECK(st.bypassed());
    const auto& in = src.buffer().channels[0];
    REQUIRE(out.size() >= in.size());
    CHECK(std::equal(in.begin(), in.end(), out.begin()));
}

TEST_CASE("Stretcher plays twice as fast at the same pitch", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 2.0, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 2.0, 0.0, true, 4 * kRate);
    CHECK_FALSE(st.bypassed());
    CHECK(soundingSeconds(out) == Catch::Approx(1.0).margin(0.08));
    CHECK(frequency(out, kRate / 4, 3 * kRate / 4) == Catch::Approx(440.0).margin(4.0));
}

TEST_CASE("Stretcher transposes without changing the length", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 1.0, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 1.0, 12.0, true, 3 * kRate);
    CHECK(soundingSeconds(out) == Catch::Approx(1.0).margin(0.08));
    CHECK(frequency(out, kRate / 4, 3 * kRate / 4) == Catch::Approx(880.0).margin(8.0));
}

TEST_CASE("Stretcher starts a stretched sound on its first frame", "[stretch]")
{
    // 50 ms of silence, then a tone: the tone must start near 50 ms, not one
    // stretch latency (about 120 ms) later.
    std::vector<float> x(static_cast<std::size_t>(kRate / 20), 0.0f);
    const auto tone = asma::test::sine(440.0, 0.5, 0.5, kRate);
    x.insert(x.end(), tone.begin(), tone.end());
    MemorySource src(mono(x));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 1.0, 0.01, false, 2 * kRate); // tiny shift: stretch on
    std::size_t first = 0;
    while (first < out.size() && std::abs(out[first]) < 0.05f) ++first;
    CHECK(static_cast<double>(first) / kRate == Catch::Approx(0.05).margin(0.02));
}

TEST_CASE("Stretcher ends once the tail is out", "[stretch]")
{
    MemorySource src(mono(asma::test::sine(440.0, 0.5, 0.5, kRate)));
    Stretcher st;
    st.prepare(kRate, kBlock);
    const auto out = play(st, src, 0.5, 0.0, true, 10 * kRate); // half speed: about 1 s
    CHECK(out.size() < static_cast<std::size_t>(2 * kRate));  // did not run on to the limit
    CHECK(soundingSeconds(out) == Catch::Approx(1.0).margin(0.1));
    float lastPeak = 0.0f; // the tail died out rather than being cut off
    for (std::size_t i = out.size() - 512; i < out.size(); ++i) lastPeak = std::max(lastPeak, std::abs(out[i]));
    CHECK(lastPeak < 0.005f);
}
