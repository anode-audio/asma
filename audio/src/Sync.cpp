// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Sync.h"

#include "asma/audio/Stretcher.h"
#include "asma/core/Library.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace asma::audio {

namespace {

struct Key {
    int pitchClass = 0; // 0 = C
    bool minor = false;
};

std::optional<Key> parseKey(std::string_view key)
{
    static constexpr std::array<std::string_view, 12> kNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    Key k;
    if (!key.empty() && key.back() == 'm') {
        k.minor = true;
        key.remove_suffix(1);
    }
    for (int i = 0; i < 12; ++i)
        if (kNames[static_cast<std::size_t>(i)] == key) {
            k.pitchClass = i;
            return k;
        }
    return std::nullopt;
}

} // namespace

std::optional<int> keyInterval(std::string_view from, std::string_view to)
{
    const auto a = parseKey(from);
    const auto b = parseKey(to);
    if (!a || !b) return std::nullopt;
    int target = b->pitchClass;
    // Different modes: aim for the project's relative key, which shares its notes.
    if (a->minor != b->minor) target = b->minor ? (target + 3) % 12 : (target + 9) % 12;
    int d = ((target - a->pitchClass) % 12 + 12) % 12;
    if (d > 5) d -= 12;
    return d;
}

SyncPlan planSync(const SampleInfo& info, const SyncSettings& settings)
{
    SyncPlan plan;
    if (settings.tempo && settings.hostBpm > 0.0 && info.isLoop.value_or(false)) {
        if (info.bpm && *info.bpm > 0.0 && info.bpmConfidence >= kMinTempoConfidence) {
            // Exactly the host tempo, however far that stretches the loop, up
            // to what the stretcher plays: the status shows what is heard.
            const double wanted = settings.hostBpm / *info.bpm;
            plan.ratio = std::clamp(wanted, Stretcher::kMinRatio, Stretcher::kMaxRatio);
            plan.tempoClamped = wanted < Stretcher::kMinRatio || wanted > Stretcher::kMaxRatio;
            plan.tempoSynced = true;
        } else {
            plan.tempoUnsure = true;
        }
    }
    if (settings.key && !settings.projectKey.empty() && info.key) {
        if (info.keyConfidence >= kMinKeyConfidence) {
            if (const auto interval = keyInterval(*info.key, settings.projectKey.view())) {
                plan.semitones = *interval;
                plan.keySynced = true;
            }
        } else {
            plan.keyUnsure = true;
        }
    }
    return plan;
}

std::int64_t framesToNextBoundary(double ppq, double bpm, int sampleRate, double beats)
{
    if (bpm <= 0.0 || beats <= 0.0) return 0;
    const double at = ppq / beats;
    const double next = std::ceil(at - 1e-9); // a host a hair past a boundary is on it
    const double beatsLeft = std::max(0.0, (next - at) * beats);
    return std::llround(beatsLeft * 60.0 / bpm * sampleRate);
}

SampleInfo sampleInfo(Library& library, std::int64_t fileId)
{
    SampleInfo info;
    if (const auto d = library.derived(fileId)) {
        info.bpm = d->bpm;
        info.bpmConfidence = d->bpmConfidence;
        info.key = d->key;
        info.keyConfidence = d->keyConfidence;
        info.isLoop = d->isLoop;
        info.rootNote = d->rootNote;
    }
    if (const auto l = library.loudness(fileId)) {
        info.lufs = l->lufs;
        info.peak = l->peak;
    }
    return info;
}

} // namespace asma::audio
