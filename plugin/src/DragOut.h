// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"
#include "asma/audio/Render.h"
#include "asma/audio/Sync.h"

#include <optional>
#include <string>

namespace asma::app {

// What dragging the selection out bakes in: its edits and what sync does to
// it now, at the host's sample rate (0 keeps the file's).
audio::RenderSettings dragSettings(const audio::Edits& edits, const audio::SyncPlan& plan, int sampleRate);

// The footer's account of a drag-out: "Drag out: the original file", or
// "Drag out renders: reversed, trimmed, stretched to 180 BPM, transposed +2".
// bpm: the sample's own tempo, to say where a stretch lands; without it the
// ratio is given.
std::string dragSummary(const audio::RenderSettings& settings, std::optional<double> bpm);

} // namespace asma::app
