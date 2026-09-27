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

namespace {
DecodedAudio audio(std::vector<float> mono, int rate = kRate)
{
    DecodedAudio a;
    a.sampleRate = rate;
    a.mono = std::move(mono);
    return a;
}
} // namespace

TEST_CASE("A drum loop gets its exact tempo and is a loop", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(128.0, 4, kRate)));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(128.0).margin(0.01));
    CHECK(r.isLoop == true);
    CHECK(r.bpmConfidence > 0.15);
}

TEST_CASE("Hats between kicks and snares do not double the tempo", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(70.0, 2, kRate)));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(70.0).margin(0.01));
}

TEST_CASE("Fast loops are not halved", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(174.0, 4, kRate)));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(174.0).margin(0.01));
}

TEST_CASE("A kick hit is a one-shot with no tempo and no key", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::kickHit(kRate)));
    CHECK(r.isLoop == false);
    CHECK_FALSE(r.bpm.has_value());
    CHECK_FALSE(r.key.has_value());
}

TEST_CASE("A single hit followed by silence is a one-shot", "[analysis]")
{
    std::vector<float> hit(static_cast<std::size_t>(2 * kRate), 0.0f);
    test::addKick(hit, 0, kRate);
    const AnalysisResult r = analyse(audio(hit));
    CHECK(r.isLoop == false);
    CHECK_FALSE(r.bpm.has_value());
}

TEST_CASE("A long decaying hit is a one-shot", "[analysis]")
{
    std::vector<float> tone = test::sine(220.0, 3.0, 0.5, kRate);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] *= static_cast<float>(std::exp(-static_cast<double>(i) / kRate / 0.25));
    const AnalysisResult r = analyse(audio(tone));
    CHECK(r.isLoop == false);
    CHECK_FALSE(r.bpm.has_value());
}

TEST_CASE("Noise and drums have no key", "[analysis]")
{
    CHECK_FALSE(estimateKey(test::noise(2.0, 0.3, kRate, 3), kRate).has_value());
    CHECK_FALSE(analyse(audio(test::drumLoop(120.0, 4, kRate))).key.has_value());
}

TEST_CASE("Audio cut short by maxSeconds is not called a loop", "[analysis]")
{
    DecodedAudio a = audio(test::drumLoop(120.0, 8, kRate));
    a.truncated = true;
    const AnalysisResult r = analyse(a);
    CHECK_FALSE(r.isLoop.has_value());
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(120.0).margin(0.5));
}

TEST_CASE("Descriptors separate bright noise from a low tone", "[analysis]")
{
    const AnalysisResult hat = analyse(audio(test::hatHit(kRate, 5)));
    const AnalysisResult kick = analyse(audio(test::kickHit(kRate)));
    CHECK(hat.centroid > 4.0 * kick.centroid);
    CHECK(hat.flatness > kick.flatness);
}

TEST_CASE("Feature vectors are complete, finite and deterministic", "[analysis]")
{
    const DecodedAudio a = audio(test::drumLoop(100.0, 2, kRate));
    const AnalysisResult first = analyse(a);
    const AnalysisResult second = analyse(a);
    REQUIRE(first.featureVector.size() == kFeatureVectorSize);
    for (float v : first.featureVector) CHECK(std::isfinite(v));
    CHECK(first.featureVector == second.featureVector);
    CHECK(first.bpm == second.bpm);
}

TEST_CASE("Very short and silent audio does not break analysis", "[analysis]")
{
    const AnalysisResult tiny = analyse(audio(std::vector<float>(10, 0.1f)));
    CHECK(tiny.isLoop == false);
    CHECK(tiny.featureVector.size() == kFeatureVectorSize);
    const AnalysisResult silent = analyse(audio(std::vector<float>(kRate * 2, 0.0f)));
    CHECK_FALSE(silent.bpm.has_value());
    CHECK(silent.featureVector.size() == kFeatureVectorSize);
    for (float v : silent.featureVector) CHECK(std::isfinite(v));
}

TEST_CASE("Other sample rates work", "[analysis]")
{
    const AnalysisResult r = analyse(audio(test::drumLoop(120.0, 2, 96000), 96000));
    REQUIRE(r.bpm.has_value());
    CHECK(*r.bpm == Catch::Approx(120.0).margin(0.01));
}
