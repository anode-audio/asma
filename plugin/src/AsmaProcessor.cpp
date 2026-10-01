// SPDX-License-Identifier: GPL-3.0-only
#include "AsmaProcessor.h"

#include "AsmaEditor.h"
#include "LibraryView.h"
#include "asma/core/Fs.h"

#include <ableton/Link.hpp>
#include <filesystem>

namespace asma::app {

AsmaProcessor::AsmaProcessor(Mode mode)
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      libraryPath_(defaultDataDir() / "library.db"),
      standalone_(mode == Mode::Standalone || wrapperType == wrapperType_Standalone)
{
    if (standalone_) {
        link_ = std::make_unique<ableton::Link>(120.0);
        const auto app = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        scans_ = std::make_unique<ScanJob>(libraryPath_, ScanJob::workerNextTo(fromUtf8(app.getFullPathName().toStdString())));
    }
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
    if (standalone_) {
        if (linkOn_.load(std::memory_order_relaxed)) {
            // Capturing the audio session state is real-time safe.
            const auto session = link_->captureAudioSessionState();
            transport.bpm = session.tempo();
            transport.ppq = session.beatAtTime(link_->clock().micros(), 4.0);
            transport.playing = session.isPlaying();
        } else {
            transport.bpm = manualBpm_.load(std::memory_order_relaxed);
        }
    } else if (auto* head = getPlayHead())
        if (const auto position = head->getPosition()) {
            transport.bpm = position->getBpm().orFallback(0.0);
            transport.ppq = position->getPpqPosition().orFallback(0.0);
            transport.playing = position->getIsPlaying();
        }
    hostBpm_.store(transport.bpm, std::memory_order_relaxed);

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

PluginState AsmaProcessor::pluginState() const
{
    const std::lock_guard lock(stateMutex_);
    return state_;
}

void AsmaProcessor::setPluginState(const PluginState& state)
{
    {
        const std::lock_guard lock(stateMutex_);
        state_ = state;
    }
    manualBpm_.store(state.sync.hostBpm, std::memory_order_relaxed);
    if (link_) link_->enable(state.link);
    linkOn_.store(standalone_ && state.link, std::memory_order_relaxed);
    engine_.setSync(state.sync);
    engine_.setGainMatch(state.gainMatch);
    engine_.setQuantise(state.quantise);
    engine_.setEdits(state.edits);
    if (!state.selected.empty()) {
        const auto path = fromUtf8(state.selected);
        std::error_code ec;
        // Its tempo and key come from the library, so a restored loop syncs.
        if (std::filesystem::exists(path, ec)) {
            LibraryView library(libraryPath_);
            library.refresh();
            engine_.select(path, library.infoFor(path), false);
        }
    }
}

void AsmaProcessor::setManualBpm(double bpm)
{
    audio::SyncSettings sync;
    updateState([&](PluginState& s) {
        s.sync.hostBpm = bpm;
        sync = s.sync;
    });
    manualBpm_.store(bpm, std::memory_order_relaxed);
    engine_.setSync(sync); // a tempo change alone does not restart playback
}

void AsmaProcessor::setLinkEnabled(bool on)
{
    updateState([&](PluginState& s) { s.link = on; });
    if (link_) link_->enable(on);
    linkOn_.store(standalone_ && on, std::memory_order_relaxed);
}

void AsmaProcessor::setLinkTempo(double bpm)
{
    if (!link_) return;
    auto session = link_->captureAppSessionState();
    session.setTempo(bpm, link_->clock().micros());
    link_->commitAppSessionState(session);
}

void AsmaProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const std::string json = toJson(pluginState());
    destData.replaceAll(json.data(), json.size());
}

void AsmaProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (!data || sizeInBytes <= 0) return;
    setPluginState(pluginStateFromJson({static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes)}));
}

} // namespace asma::app

// The entry point every plugin format's wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new asma::app::AsmaProcessor(); }
