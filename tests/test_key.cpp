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

TEST_CASE("Chord progressions give their key", "[key]")
{
    CHECK(estimateKey(test::chordProgression(0, false, kRate), kRate)->key == "C");
    CHECK(estimateKey(test::chordProgression(9, true, kRate), kRate)->key == "Am");
    CHECK(estimateKey(test::chordProgression(6, true, kRate), kRate)->key == "F#m");
}

TEST_CASE("Noise has no key", "[key]")
{
    CHECK_FALSE(estimateKey(test::noise(2.0, 0.3, kRate, 3), kRate).has_value());
}

TEST_CASE("A quarter of a second or less has no key", "[key]")
{
    CHECK_FALSE(estimateKey(test::sine(440.0, 0.2, 0.5, kRate), kRate).has_value());
}
