// SPDX-License-Identifier: GPL-3.0-only
#include "TempoChip.h"

#include <cmath>

namespace asma::app {

std::string bpmText(double bpm)
{
    const long long tenths = std::llround(bpm * 10.0);
    std::string text = std::to_string(tenths / 10);
    if (tenths % 10 != 0) text += "." + std::to_string(std::llabs(tenths % 10));
    return text;
}

std::string ratioText(double ratio)
{
    const long long hundredths = std::llround(ratio * 100.0);
    const long long cents = hundredths % 100;
    return std::to_string(hundredths / 100) + "." + (cents < 10 ? "0" : "") + std::to_string(cents);
}

std::string secondsText(double seconds) { return ratioText(seconds) + " s"; }

std::string lengthText(double seconds)
{
    if (std::llround(seconds * 100.0) < 6000) return secondsText(seconds); // under 60.00 s as shown
    const long long total = std::llround(seconds);
    const long long h = total / 3600, m = total / 60 % 60, s = total % 60;
    const auto two = [](long long v) { return (v < 10 ? "0" : "") + std::to_string(v); };
    return h > 0 ? std::to_string(h) + ":" + two(m) + ":" + two(s) : std::to_string(m) + ":" + two(s);
}

ChipText tempoChip(const audio::SampleInfo& info, const audio::SyncSettings& sync, bool failed)
{
    if (failed) return {"can't read this file", Tone::Warning};
    if (!sync.tempo) return {"off", Tone::Muted};
    if (!info.isLoop) return {"not a loop · plays as is", Tone::Muted};
    if (!*info.isLoop) return {"one-shot · plays as is", Tone::Muted};
    if (sync.hostBpm <= 0.0) return {"no tempo · plays as is", Tone::Muted};
    const audio::SyncPlan plan = audio::planSync(info, sync);
    if (!plan.tempoSynced) {
        const std::string guess = info.bpm ? "~" + std::to_string(std::llround(*info.bpm)) + " " : "";
        return {guess + "? · plays as is", Tone::Warning};
    }
    std::string text = bpmText(*info.bpm) + " → " + bpmText(*info.bpm * plan.ratio) + " · x" + ratioText(plan.ratio);
    if (plan.tempoClamped) return {text + " max", Tone::Warning};
    return {text, Tone::Synced};
}

} // namespace asma::app
