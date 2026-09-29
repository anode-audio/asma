// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "TestUtil.h"
#include "asma/audio/Gain.h"
#include "asma/audio/Loader.h"
#include "asma/core/Analysis.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;

namespace {

SampleInfo loudness(double lufs, double peak)
{
    SampleInfo s;
    s.lufs = lufs;
    s.peak = peak;
    return s;
}

double db(float gain) { return 20.0 * std::log10(gain); }

} // namespace

TEST_CASE("matchGain brings samples to the target loudness", "[gain]")
{
    CHECK(db(matchGain(loudness(-16.0, 0.3))) == Catch::Approx(0.0).margin(1e-6));
    CHECK(db(matchGain(loudness(-10.0, 0.9))) == Catch::Approx(-6.0));
    CHECK(db(matchGain(loudness(-4.0, 1.0))) == Catch::Approx(-12.0)); // cuts are not limited
    CHECK(db(matchGain(loudness(-22.0, 0.1))) == Catch::Approx(6.0));
}

TEST_CASE("matchGain boosts at most 12 dB and never past -1 dBFS", "[gain]")
{
    CHECK(db(matchGain(loudness(-70.0, 0.001))) == Catch::Approx(kMaxBoostDb)); // near silence
    // Quiet but peaky: +10 dB wanted, the peak allows only 0.891 / 0.5.
    CHECK(matchGain(loudness(-26.0, 0.5)) == Catch::Approx(kPeakCeiling / 0.5));
    CHECK(matchGain(loudness(-26.0, 1.0)) == 1.0f); // already at full scale: no boost, no cut
    SampleInfo noPeak;
    noPeak.lufs = -22.0;
    CHECK(db(matchGain(noPeak)) == Catch::Approx(6.0));
}

TEST_CASE("matchGain leaves unmeasured samples alone", "[gain]")
{
    CHECK(matchGain({}) == 1.0f);
}

TEST_CASE("Loader measures a short file the library has not analysed", "[gain]")
{
    TempDir dir;
    const auto shortFile = dir.path() / "tone.wav";
    const auto longFile = dir.path() / "long.wav";
    const auto tone = test::sine(440.0, 1.0, 0.25, 44100);
    test::writeWavFloat(shortFile, 44100, {tone, tone});
    test::writeWavFloat(longFile, 1000, {test::ramp(20000)});
    PreviewCache cache;
    Loader loader(cache);

    loader.select(shortFile);
    loader.pump();
    Preview* p = loader.takeReady();
    REQUIRE(p);
    REQUIRE(p->info.lufs);
    CHECK(*p->info.lufs == Catch::Approx(measureLoudness(tone, 44100).lufs));
    CHECK(*p->info.peak == Catch::Approx(0.25).margin(1e-3));

    SampleInfo known;
    known.lufs = -3.0;
    loader.select(shortFile, known);
    loader.pump();
    CHECK(loader.takeReady()->info.lufs == -3.0); // the library's value stands

    loader.select(longFile);
    loader.pump();
    CHECK_FALSE(loader.takeReady()->info.lufs); // streamed: plays at unity
}
