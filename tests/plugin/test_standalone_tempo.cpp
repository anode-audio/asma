// SPDX-License-Identifier: GPL-3.0-only
#include "PluginTestUtil.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using asma::app::AsmaProcessor;

namespace {

void block(AsmaProcessor& p)
{
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
}

} // namespace

TEST_CASE("the standalone plays at its manual tempo", "[standalone]")
{
    AsmaProcessor p(AsmaProcessor::Mode::Standalone);
    CHECK(p.isStandalone());
    p.prepareToPlay(48000.0, 512);
    asma::app::PluginState s;
    s.sync.hostBpm = 93.0;
    p.setPluginState(s);
    block(p);
    CHECK(p.hostBpm() == 93.0);
    p.setManualBpm(128.0);
    block(p);
    CHECK(p.hostBpm() == 128.0);
    CHECK(p.pluginState().sync.hostBpm == 128.0);
}

TEST_CASE("the standalone follows Ableton Link when it is on", "[standalone]")
{
    AsmaProcessor p(AsmaProcessor::Mode::Standalone);
    p.prepareToPlay(48000.0, 512);
    p.setManualBpm(100.0);
    p.setLinkEnabled(true);
    CHECK(p.pluginState().link);
    p.setLinkTempo(137.0); // what a peer (or asma's own tempo field) would set
    block(p);
    CHECK(p.hostBpm() == Catch::Approx(137.0));
    p.setLinkEnabled(false);
    block(p);
    CHECK(p.hostBpm() == 100.0);
}

TEST_CASE("a plugin ignores the standalone's tempo settings", "[standalone]")
{
    AsmaProcessor p; // inside a host
    CHECK_FALSE(p.isStandalone());
    p.prepareToPlay(48000.0, 512);
    p.setManualBpm(128.0);
    p.setLinkEnabled(true);
    block(p);
    CHECK(p.hostBpm() == 0.0); // no play head, no host tempo
}

TEST_CASE("a fresh standalone plays at the tempo it shows", "[standalone]")
{
    AsmaProcessor p(AsmaProcessor::Mode::Standalone); // no saved state at all
    p.prepareToPlay(48000.0, 512);
    block(p);
    CHECK(p.hostBpm() == 120.0);
    CHECK(p.pluginState().sync.hostBpm == 120.0);

    asma::app::PluginState old; // a state without a manual tempo
    p.setPluginState(old);
    block(p);
    CHECK(p.hostBpm() == 120.0);
}
