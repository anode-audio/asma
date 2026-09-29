// SPDX-License-Identifier: GPL-3.0-only
#include "TestUtil.h"
#include "asma/audio/SampleSource.h"
#include "asma/core/AudioProbe.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using namespace asma::audio;
using asma::test::TempDir;

TEST_CASE("loadAudio decodes every frame of every channel", "[source]")
{
    TempDir dir;
    const auto p = dir.path() / "s.wav";
    const auto left = test::ramp(3000);
    const auto right = test::ramp(3000, 0.0f, 0.5f);
    test::writeWavFloat(p, 22050, {left, right});
    const AudioBuffer b = loadAudio(p);
    CHECK(b.sampleRate == 22050);
    CHECK(b.channelCount() == 2);
    CHECK(b.frames() == 3000);
    CHECK(b.bytes() == 2 * 3000 * sizeof(float));
    CHECK(b.channels[0] == left);
    CHECK(b.channels[1] == right);
    CHECK_THROWS_AS(loadAudio(dir.path() / "gone.wav"), FileAccessError);
}

TEST_CASE("MemorySource zero-fills outside the file", "[source]")
{
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->sampleRate = 44100;
    buffer->channels = {{1, 2, 3, 4}};
    MemorySource source(buffer);
    CHECK(source.frames() == 4);
    CHECK(source.channels() == 1);

    std::vector<float> out(6, -1.0f);
    float* dst[] = {out.data()};
    CHECK(source.read(-2, 6, dst));
    CHECK(out == std::vector<float>{0, 0, 1, 2, 3, 4});
    CHECK(source.read(2, 6, dst));
    CHECK(out == std::vector<float>{3, 4, 0, 0, 0, 0});
    CHECK(source.read(10, 6, dst)); // entirely past the end
    CHECK(out == std::vector<float>(6, 0.0f));
    CHECK(source.read(-10, 6, dst)); // entirely before the start
    CHECK(out == std::vector<float>(6, 0.0f));
}
