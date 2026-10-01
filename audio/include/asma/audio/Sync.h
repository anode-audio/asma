// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace asma {
class Library;
}

namespace asma::audio {

// Below these, a value is a guess: the sample plays unmodified and the UI
// shows "?". File-name (0.9) and embedded (1.0) values always pass. Measured
// on labelled libraries: loop tempos at 0.3 and up were right or an octave
// off (which sync absorbs) 52 times in 53; keys at 0.7 and up were right or
// the relative key about 9 times in 10 on loops and synths. Guitar recordings
// scored far lower at every confidence, which is why key sync starts off.
constexpr double kMinTempoConfidence = 0.3;
constexpr double kMinKeyConfidence = 0.7;

// A canonical key ("C#m" at most) held in place, so settings that carry one
// reach the audio thread without allocating.
class KeyName {
public:
    KeyName() = default;
    KeyName(std::string_view key) // NOLINT: implicit, so settings.projectKey = "Am" reads naturally
    {
        const std::size_t n = key.size() < sizeof(text_) ? key.size() : sizeof(text_) - 1;
        for (std::size_t i = 0; i < n; ++i) text_[i] = key[i];
    }
    KeyName(const char* key) : KeyName(std::string_view(key)) {}
    std::string_view view() const { return text_; }
    bool empty() const { return text_[0] == '\0'; }

private:
    char text_[4] = {};
};

struct SyncSettings {
    bool tempo = true;     // stretch loops to the host tempo
    bool key = false;      // transpose to the project key
    double hostBpm = 0.0;  // 0 when unknown: no tempo sync
    KeyName projectKey;    // empty: no key sync
};

struct SyncPlan {
    double ratio = 1.0;     // input frames per output frame, for Stretcher::setTiming
    double semitones = 0.0;
    bool tempoSynced = false;
    bool keySynced = false;
    // Sync was wanted but the sample's own value is too uncertain.
    bool tempoUnsure = false;
    bool keyUnsure = false;
};

// Tempo sync applies to loops only. The ratio is hostBpm / bpm, clamped to
// the stretcher's range, so a loop at 70 plays twice as fast at 140. The
// transposition is the shortest interval, -6..+5 semitones, to the project
// key or, when the modes differ, to its relative key: Am into C stays put.
SyncPlan planSync(const SampleInfo& info, const SyncSettings& settings);

// Semitones from one canonical key to another, as planSync uses them;
// nullopt when either key is not canonical.
std::optional<int> keyInterval(std::string_view from, std::string_view to);

// Output frames from `ppq` (host position in quarter notes) to the next
// multiple of `beats`; 0 when exactly on one.
std::int64_t framesToNextBoundary(double ppq, double bpm, int sampleRate, double beats);

// What the library knows about a file, for audition.
SampleInfo sampleInfo(Library& library, std::int64_t fileId);

} // namespace asma::audio
