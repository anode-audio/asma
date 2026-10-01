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
    // Kept rather than read back with getSampleRate(), which is 0 until a
    // wrapper has called setRateAndBufferSizeDetails.
    sampleRate_ = sampleRate;
    engine_.prepare(static_cast<int>(sampleRate), samplesPerBlock);
}

bool AsmaProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AsmaProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    audio::Transport transport;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition()) {
            transport.bpm = position->getBpm().orFallback(0.0);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
        }

    // Render up to each MIDI event, so a note starts on its own sample and a
    // quantised start still sees the right position.
    int done = 0;
    const auto renderTo = [&](int end) {
        if (end <= done) return;
        audio::Transport t = transport;
        t.ppq += done * t.bpm / (60.0 * sampleRate_);
        engine_.setTransport(t);
        float* out[2] = {buffer.getWritePointer(0, done), buffer.getWritePointer(1, done)};
        engine_.process(out, end - done);
        done = end;
    };
    for (const auto event : midi) {
        renderTo(std::clamp(event.samplePosition, done, n));
        const juce::MidiMessage m = event.getMessage();
        if (m.isNoteOn()) engine_.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine_.noteOff(m.getNoteNumber()); // includes note-on at velocity 0
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine_.allNotesOff();
    }
    renderTo(n);
}

juce::AudioProcessorEditor* AsmaProcessor::createEditor() { return new AsmaEditor(*this); }

void AsmaProcessor::getStateInformation(juce::MemoryBlock&) {}

void AsmaProcessor::setStateInformation(const void*, int) {}

} // namespace asma::app

// The entry point every plugin format's wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new asma::app::AsmaProcessor(); }
