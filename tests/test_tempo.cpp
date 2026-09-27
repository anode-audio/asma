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

TEST_CASE("Free tempo estimates of drum loops", "[tempo]")
{
    for (double bpm : {70.0, 100.0, 128.0, 174.0}) {
        INFO(bpm);
        const auto t = estimateTempo(test::drumLoop(bpm, 4, kRate), kRate);
        REQUIRE(t.has_value());
        CHECK(t->bpm == Catch::Approx(bpm).margin(1.0));
        CHECK(t->confidence > 0.5);
    }
}

TEST_CASE("Shaker loops with nothing below 1.5 kHz still get a tempo", "[tempo]")
{
    std::vector<float> shaker(static_cast<std::size_t>(8 * kRate));
    test::Noise noise(3);
    for (int i = 0; i < 32; ++i) test::addHat(shaker, static_cast<std::size_t>(i * 0.25 * kRate), kRate, noise, 0.5);
    const auto t = estimateTempo(shaker, kRate);
    REQUIRE(t.has_value());
    CHECK(t->bpm == Catch::Approx(120.0).margin(1.0));
}

TEST_CASE("No tempo for under a second or for silence", "[tempo]")
{
    CHECK_FALSE(estimateTempo(std::vector<float>(kRate / 2, 0.1f), kRate).has_value());
    CHECK_FALSE(estimateTempo(std::vector<float>(2 * kRate, 0.0f), kRate).has_value());
}
