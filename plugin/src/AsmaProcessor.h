// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "PluginState.h"
#include "ScanJob.h"
#include "asma/audio/AuditionEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>

namespace ableton {
class Link;
}

namespace asma::app {

// The plugin and the standalone: an instrument with MIDI in and stereo out
// whose audio is the audition engine's.
class AsmaProcessor : public juce::AudioProcessor {
public:
    // FromWrapper: whatever JUCE's wrapper says; Standalone forces the
    // standalone's behaviour, for tests.
    enum class Mode { FromWrapper, Standalone };
    explicit AsmaProcessor(Mode mode = Mode::FromWrapper);
    ~AsmaProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "asma"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    audio::AuditionEngine& engine() { return engine_; }

    // Message thread. Auditions a file as it is, without the last one's edits,
    // and remembers it as the project's selection.
    std::uint64_t select(const std::filesystem::path& path, const audio::SampleInfo& info);
    // Message thread. The selection's edits: saved with the project and
    // applied at once, restarting what plays.
    void setEdits(const audio::Edits& edits);

    // Any thread but the audio thread. Setting applies the settings to the
    // engine and selects the saved sample again, without playing it.
    PluginState pluginState() const;
    void setPluginState(const PluginState& state);
    // Counts setPluginState calls, so an open editor can tell it must reload.
    std::uint64_t stateLoads() const { return stateLoads_.load(); }
    // Records what the UI changed; the UI tells the engine itself.
    template <typename Change>
    void updateState(Change&& change)
    {
        const std::lock_guard lock(stateMutex_);
        change(state_);
    }

    // <data dir>/library.db, fixed when the processor is made.
    const std::filesystem::path& libraryPath() const { return libraryPath_; }
    // The host's tempo in the last block, 0 when it gave none.
    double hostBpm() const { return hostBpm_.load(std::memory_order_relaxed); }
    // Message thread. The tempo sync works to: the last block's, else, in the
    // standalone before any audio has run, Link's or the manual one. 0 for
    // none. The footer and the drag both ask here, so they always agree.
    double tempoInForce();
    double sampleRate() const { return sampleRate_; }

    // The standalone has no host: its tempo is Ableton Link's when Link is
    // on, else the manual tempo. A plugin keeps these but uses the host's.
    bool isStandalone() const { return standalone_; }
    void setManualBpm(double bpm);
    void setLinkEnabled(bool on);
    // Proposes a tempo to the Link session (peers may change it again).
    void setLinkTempo(double bpm);
    // Adding folders and scanning: the standalone only; null in a plugin,
    // which never writes the library from inside the host.
    ScanJob* scans() { return scans_.get(); }

private:
    // One preview cache for every instance in the process.
    juce::SharedResourcePointer<audio::PreviewCache> cache_;
    audio::AuditionEngine engine_{*cache_};
    double sampleRate_ = 44100.0;
    std::filesystem::path libraryPath_;
    std::atomic<double> hostBpm_{0.0};
    const bool standalone_;
    std::unique_ptr<ableton::Link> link_; // standalone only
    std::unique_ptr<ScanJob> scans_;      // standalone only
    static constexpr double kDefaultBpm = 120.0; // the standalone's tempo until set
    std::atomic<bool> linkOn_{false};
    std::atomic<double> manualBpm_{0.0};
    mutable std::mutex stateMutex_; // hosts may save state off the message thread
    PluginState state_;
    std::atomic<std::uint64_t> stateLoads_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AsmaProcessor)
};

} // namespace asma::app
