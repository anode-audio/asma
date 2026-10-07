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
        // Plays at the tempo its BPM field shows from the first launch.
        state_.sync.hostBpm = kDefaultBpm;
        manualBpm_.store(kDefaultBpm, std::memory_order_relaxed);
        link_ = std::make_unique<ableton::Link>(kDefaultBpm);
    }
    // In a plugin, the plugin's own binary (where JUCE can tell), so the
    // helpers are found inside its bundle.
    const auto binary = fromUtf8(
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString());
    if (standalone_) {
        scans_ = std::make_unique<ScanJob>(libraryPath_, ScanJob::workerNextTo(binary));
        writer_ = std::make_unique<DirectWriter>(libraryPath_, LibraryWriter::cliNextTo(binary));
    } else {
        writer_ = std::make_unique<CliWriter>(libraryPath_, LibraryWriter::cliNextTo(binary));
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

void AsmaProcessor::setPluginState(const PluginState& given)
{
    PluginState state = given;
    if (standalone_ && state.sync.hostBpm <= 0.0) state.sync.hostBpm = kDefaultBpm; // a state that never set one
    {
        const std::lock_guard lock(stateMutex_);
        state_ = state;
    }
    stateLoads_.fetch_add(1);
    manualBpm_.store(state.sync.hostBpm, std::memory_order_relaxed);
    if (link_) link_->enable(state.link);
    linkOn_.store(standalone_ && state.link, std::memory_order_relaxed);
    engine_.setSync(state.sync);
    engine_.setGainMatch(state.gainMatch);
    engine_.setQuantise(state.quantise);
    engine_.setEdits(state.edits);
    std::filesystem::path path;
    try {
        path = fromUtf8(state.selected); // throws on Windows for bytes that are not UTF-8
    } catch (const std::exception&) {
        path.clear();
    }
    std::error_code ec;
    if (!path.empty() && std::filesystem::exists(path, ec)) {
        // Its tempo and key come from the library, so a restored loop syncs.
        LibraryView library(libraryPath_);
        library.refresh();
        engine_.select(path, library.infoFor(path), false);
    } else {
        // Nothing to play: drop the old selection rather than keep sounding
        // a sample the project does not name. The saved path stays.
        engine_.select(path, {}, false);
    }
}

std::uint64_t AsmaProcessor::select(const std::filesystem::path& path, const audio::SampleInfo& info)
{
    updateState([&](PluginState& s) {
        s.selected = toUtf8(path);
        s.edits = {};
    });
    engine_.setEdits({}, false); // the old sample fades out as it was
    return engine_.select(path, info, true);
}

void AsmaProcessor::setEdits(const audio::Edits& edits)
{
    updateState([&](PluginState& s) { s.edits = edits; });
    engine_.setEdits(edits);
}

double AsmaProcessor::tempoInForce()
{
    if (const double host = hostBpm(); host > 0.0) return host;
    if (!standalone_) return 0.0;
    if (linkOn_.load(std::memory_order_relaxed) && link_) return link_->captureAppSessionState().tempo();
    return manualBpm_.load(std::memory_order_relaxed);
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
    // Whatever a damaged project holds must not escape into the host.
    try {
        setPluginState(pluginStateFromJson({static_cast<const char*>(data), static_cast<std::size_t>(sizeInBytes)}));
    } catch (const std::exception&) {
    }
}

} // namespace asma::app

// The entry point every plugin format's wrapper calls.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new asma::app::AsmaProcessor(); }
