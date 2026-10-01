// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaProcessor.h"

#include "AsmaEditor.h"

namespace asma::app {

AsmaProcessor::AsmaProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    engine_.loader().start();
}

AsmaProcessor::~AsmaProcessor() = default;

void AsmaProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.prepare(static_cast<int>(sampleRate), samplesPerBlock);
}

bool AsmaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AsmaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessorEditor* AsmaProcessor::createEditor() { return new AsmaEditor(*this); }

void AsmaProcessor::getStateInformation(juce::MemoryBlock&) {}

void AsmaProcessor::setStateInformation(const void*, int) {}

} // namespace asma::app

// The entry point every plugin format's wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new asma::app::AsmaProcessor(); }
