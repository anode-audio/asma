// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"
#include "asma/audio/Loader.h"
#include "asma/audio/PlayHead.h"
#include "asma/audio/SpscQueue.h"
#include "asma/audio/Stretcher.h"
#include "asma/audio/Sync.h"
#include "asma/audio/Voices.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace asma::audio {

// The host's (or the standalone's) timeline, once per block.
struct Transport {
    double bpm = 0.0; // 0 when unknown
    double ppq = 0.0; // position in quarter notes at the start of the block
    bool playing = false;
};

// A snapshot for the UI.
struct EngineStatus {
    std::uint64_t generation = 0; // the preview the audio thread holds
    bool playing = false;
    double position = 0.0; // seconds into the file
    double ratio = 1.0;
    double semitones = 0.0;
    bool tempoSynced = false, keySynced = false;
    bool tempoUnsure = false, keyUnsure = false; // show "?"
    bool failed = false; // the selection's file could not be opened
    int voices = 0;
};

// Audition as one object: the loader, one previewing chain (PlayHead,
// Stretcher, gain, stop fade) and the MIDI voices.
//
// Threads: prepare() and the control calls from one control thread; the
// audio calls from the audio thread; status() from anywhere. Control calls
// reach the audio thread through a queue and take effect at its next block.
// An edit while playing restarts the preview; switching samples fades the
// old one out over 5 ms first.
class AuditionEngine {
public:
    static constexpr double kStopFadeSeconds = 0.005;
    static constexpr double kMaxPrimeSeconds = 0.1; // longest wait for a streamed start

    explicit AuditionEngine(PreviewCache& cache) : loader_(cache) {}

    Loader& loader() { return loader_; }

    // Control thread, while the audio thread is not running. Allocates.
    void prepare(int sampleRate, int maxBlock);

    // Control thread.
    std::uint64_t select(const std::filesystem::path& path, SampleInfo info, bool autoplay);
    // The newest selection's generation; status() reports on it once the
    // audio thread holds it.
    std::uint64_t selected() const { return selected_.load(); }
    // The newest selection's waveform once the loader has built it. Any thread.
    std::shared_ptr<const Overview> overview() const { return loader_.overview(selected()); }
    void play();
    void stop();
    void setEdits(const Edits& edits);
    // hostBpm here is the standalone's manual tempo; a transport tempo wins.
    void setSync(const SyncSettings& sync);
    void setGainMatch(bool on);
    // Start on the next multiple of `beats` while the transport plays; 0 is off.
    void setQuantise(double beats);

    // Audio thread.
    void setTransport(const Transport& transport) { transport_ = transport; }
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void allNotesOff();
    // Writes n stereo frames; more than maxBlock is fine.
    void process(float* const* out, int n);

    EngineStatus status() const;

private:
    enum class State { Idle, Waiting, Playing, Stopping };
    struct Command {
        enum class Type { Play, Stop, Edits, Sync, GainMatch, Quantise } type = Type::Play;
        std::uint64_t generation = 0;
        Edits edits;
        SyncSettings sync;
        bool flag = false;
        double value = 0.0;
    };

    void processChunk(float* const* out, int n, bool first);
    void push(const Command& command);
    void handle(const Command& command);
    void adopt(Preview* preview);
    // Starts the current preview now, on the next beat or bar, or after the
    // audible one has faded out. `offset`: frames into this block, where
    // rendering stands when the request is made.
    void requestStart(int offset);
    // False while a streamed start is still loading.
    bool startNow();
    void beginStop();
    double bpm() const { return transport_.bpm > 0.0 ? transport_.bpm : sync_.hostBpm; }
    SyncPlan plan(const Preview& preview) const;
    void publish();

    Loader loader_;
    SpscQueue<Command, 64> commands_;
    std::atomic<std::uint64_t> selected_{0}; // control thread's latest selection

    // Audio thread state.
    int sampleRate_ = 44100;
    int maxBlock_ = 512;
    PlayHead head_;
    Stretcher stretcher_;
    VoicePool voices_;
    std::array<std::vector<float>, 2> play_, mix_;
    Transport transport_;
    Edits edits_;
    SyncSettings sync_;
    bool gainMatch_ = true;
    double quantise_ = 0.0;
    Preview* current_ = nullptr; // the selection: what play() plays and MIDI notes use
    Preview* playing_ = nullptr; // what the previewing chain renders (the old one during a switch)
    std::uint64_t playWanted_ = 0; // generation play() asked for before it arrived
    State state_ = State::Idle;
    bool restartAfterFade_ = false;
    std::uint64_t stoppedThrough_ = 0; // stop() covered selections up to this generation
    int chunkOffset_ = 0;              // where the current chunk starts in the host's block
    double primed_ = 0.0;              // frames waited so far for a streamed start
    std::int64_t wait_ = 0; // frames until a quantised start
    int fade_ = 0;          // frames left in the stop fade
    int fadeFrames_ = 1;
    float gain_ = 1.0f;
    SyncPlan plan_;

    // Published for status().
    std::atomic<std::uint64_t> statusGeneration_{0};
    std::atomic<bool> statusPlaying_{false};
    std::atomic<double> statusPosition_{0.0};
    std::atomic<double> statusRatio_{1.0};
    std::atomic<double> statusSemitones_{0.0};
    std::atomic<int> statusFlags_{0};
    std::atomic<int> statusVoices_{0};
};

} // namespace asma::audio
