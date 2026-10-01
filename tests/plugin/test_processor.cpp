// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

using asma::app::AsmaProcessor;

TEST_CASE("the processor is a stereo instrument that takes MIDI", "[processor]")
{
    AsmaProcessor p;
    CHECK(p.getName() == "asma");
    CHECK(p.acceptsMidi());
    CHECK_FALSE(p.producesMidi());
    CHECK(p.getTotalNumInputChannels() == 0);
    CHECK(p.getTotalNumOutputChannels() == 2);
    AsmaProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK_FALSE(p.checkBusesLayoutSupported(mono));
}

TEST_CASE("the processor is silent with nothing selected", "[processor]")
{
    AsmaProcessor p;
    p.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> buffer(2, 512);
    buffer.applyGain(0.0f);
    buffer.setSample(0, 10, 0.5f); // anything the host left there goes
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi);
    CHECK_FALSE(buffer.getMagnitude(0, 512) > 0.0f);
}

TEST_CASE("the editor opens", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    AsmaProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditorAndMakeActive());
    REQUIRE(editor);
    CHECK(editor->getWidth() > 0);
    p.editorBeingDeleted(editor.get());
}
