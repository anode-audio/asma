// SPDX-License-Identifier: GPL-3.0-only
#include "PluginTestUtil.h"
#include "Signals.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>

using asma::app::AsmaProcessor;
using asma::test::TempDir;
namespace fs = std::filesystem;

namespace {

constexpr int kRate = 48000;
constexpr int kBlock = 512;

std::vector<float> constant(int frames, float value) { return std::vector<float>(static_cast<std::size_t>(frames), value); }

struct Rig {
    TempDir dir;
    AsmaProcessor p;
    juce::AudioBuffer<float> buffer{2, kBlock};
    juce::MidiBuffer midi;
    Rig()
    {
        p.prepareToPlay(kRate, kBlock);
        p.engine().setGainMatch(false); // exact levels
    }
    std::uint64_t select(const std::vector<float>& samples, asma::audio::SampleInfo info = {}, bool autoplay = false)
    {
        const auto path = dir.path() / ("s" + std::to_string(counter++) + ".wav");
        asma::test::writeWavFloat(path, kRate, {samples});
        const auto g = p.engine().select(path, info, autoplay);
        REQUIRE(asma::test::waitForPreview(p, g));
        return g;
    }
    void block()
    {
        p.processBlock(buffer, midi);
        midi.clear();
    }
    int counter = 0;
};

} // namespace

TEST_CASE("MIDI notes sound from the sample they arrive on", "[processor]")
{
    Rig rig;
    rig.select(constant(kRate, 0.5f));
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 300);
    rig.block();
    CHECK(rig.buffer.getSample(0, 299) == 0.0f);
    CHECK(rig.buffer.getSample(0, 300) > 0.0f);           // the attack starts here
    CHECK(rig.buffer.getSample(1, 450) == Catch::Approx(0.5f)); // past the 2 ms attack, both sides
    CHECK(rig.p.engine().status().voices == 1);
}

TEST_CASE("note-off, velocity-0 note-on and all-notes-off all release", "[processor]")
{
    Rig rig;
    rig.select(constant(kRate, 0.5f));
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 62, 1.0f), 0);
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
    rig.block();
    CHECK(rig.p.engine().status().voices == 3);
    rig.midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    rig.midi.addEvent(juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(0)), 0);
    for (int i = 0; i < 10; ++i) rig.block(); // 80 ms release
    CHECK(rig.p.engine().status().voices == 1);
    rig.midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    for (int i = 0; i < 10; ++i) rig.block();
    CHECK(rig.p.engine().status().voices == 0);
}

TEST_CASE("the host's tempo reaches the engine", "[processor]")
{
    Rig rig;
    asma::test::FakePlayHead host;
    host.bpm = 90.0;
    host.playing = true;
    rig.p.setPlayHead(&host);
    asma::audio::SampleInfo loop;
    loop.isLoop = true;
    loop.bpm = 120.0;
    loop.bpmConfidence = 0.9;
    rig.select(asma::test::sine(220.0, 2.0, 0.5, kRate), loop, true);
    rig.block();
    CHECK(rig.p.engine().status().tempoSynced);
    CHECK(rig.p.engine().status().ratio == Catch::Approx(0.75));
    host.bpm = 150.0;
    rig.block();
    CHECK(rig.p.engine().status().ratio == Catch::Approx(1.25));
    rig.p.setPlayHead(nullptr);
}

TEST_CASE("a quantised start lands on the bar inside a block split by MIDI", "[processor]")
{
    Rig rig;
    asma::test::FakePlayHead host;
    host.bpm = 120.0;
    host.ppq = 3.99; // 0.01 beats before bar 2: 240 frames at 48 kHz
    host.playing = true;
    rig.p.setPlayHead(&host);
    rig.p.engine().setQuantise(4.0);
    rig.select(constant(kRate, 0.25f), {}, false);
    rig.p.engine().play();
    rig.midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 100); // splits the block at 100
    rig.block();
    int first = 0;
    while (first < kBlock && rig.buffer.getSample(0, first) == 0.0f) ++first;
    CHECK(first == 240);
    rig.p.setPlayHead(nullptr);
}
