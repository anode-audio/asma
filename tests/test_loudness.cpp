// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;

namespace {
constexpr int kRate = 44100;
} // namespace

TEST_CASE("Loudness of a 1 kHz sine matches EBU R128", "[loudness]")
{
    // A full-scale 1 kHz sine is -3.01 LUFS; amplitude 0.1 is 20 dB lower.
    const Loudness long_ = measureLoudness(test::sine(1000.0, 2.0, 0.1, kRate), kRate);
    CHECK(long_.lufs == Catch::Approx(-23.01).margin(0.1));
    CHECK(long_.peak == Catch::Approx(0.1).margin(1e-3));
}

TEST_CASE("Sounds under 400 ms are measured ungated", "[loudness]")
{
    const Loudness shortSine = measureLoudness(test::sine(1000.0, 0.2, 0.1, kRate), kRate);
    CHECK(shortSine.lufs == Catch::Approx(-23.01).margin(0.5));
}

TEST_CASE("Silence has the loudness floor", "[loudness]")
{
    const Loudness silence = measureLoudness(std::vector<float>(kRate, 0.0f), kRate);
    CHECK(silence.lufs == -70.0);
    CHECK(silence.peak == 0.0);
}
