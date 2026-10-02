// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"
#include "asma/audio/Sync.h"
#include "asma/core/Query.h"

#include <string>
#include <string_view>

namespace asma::app {

// What a project remembers: the selected sample, the search (as a saved
// search would store it) and view state. Never the library itself.
struct PluginState {
    std::string selected; // UTF-8 path; empty when nothing is selected
    SearchModel search;
    audio::SyncSettings sync; // hostBpm: the standalone's manual tempo
    bool link = false;        // the standalone follows Ableton Link
    bool gainMatch = true;
    double quantise = 0.0; // beats; 0 is off
    audio::Edits edits;    // of the selected sample
    int width = 1100; // the window; spec section 9, Window
    int height = 720;
};

std::string toJson(const PluginState& state);

// Reads what toJson wrote, possibly by another asma version: unknown fields,
// wrong types and out-of-range values are skipped, and anything that is not a
// JSON object gives the defaults.
PluginState pluginStateFromJson(std::string_view json);

} // namespace asma::app
