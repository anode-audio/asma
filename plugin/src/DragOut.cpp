// SPDX-License-Identifier: GPL-3.0-only
#include "DragOut.h"

#include "TempoChip.h"

#include <cmath>
#include <vector>

namespace asma::app {

audio::RenderSettings dragSettings(const audio::Edits& edits, const audio::SyncPlan& plan, int sampleRate)
{
    audio::RenderSettings settings;
    settings.edits = edits;
    settings.ratio = plan.ratio;
    settings.semitones = plan.semitones;
    settings.sampleRate = sampleRate;
    return settings;
}

std::string dragSummary(const audio::RenderSettings& settings, std::optional<double> bpm)
{
    if (!settings.changesAudio()) return "Drag out: the original file";
    std::vector<std::string> parts;
    if (settings.edits.direction == audio::Direction::Reverse) parts.push_back("reversed");
    if (settings.edits.direction == audio::Direction::PingPong) parts.push_back("ping-pong");
    if (settings.edits.trimStart > 0.0 || settings.edits.trimEnd >= 0.0) parts.push_back("trimmed");
    if (settings.ratio < 1.0 || settings.ratio > 1.0)
        parts.push_back(bpm ? "stretched to " + bpmText(*bpm * settings.ratio) + " BPM" : "stretched x" + ratioText(settings.ratio));
    if (settings.semitones < 0.0 || settings.semitones > 0.0) {
        const long long st = std::llround(settings.semitones);
        parts.push_back("transposed " + std::string(st > 0 ? "+" : "") + std::to_string(st));
    }
    std::string text = "Drag out renders: ";
    for (std::size_t i = 0; i < parts.size(); ++i) text += (i ? ", " : "") + parts[i];
    return text;
}

} // namespace asma::app
