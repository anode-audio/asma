// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"
#include "asma/audio/Sync.h"

#include <string>

namespace asma::app {

// How the chip is coloured: green, amber or grey in the Anode theme.
enum class Tone { Synced, Warning, Muted };

struct ChipText {
    std::string text; // UTF-8
    Tone tone = Tone::Muted;
};

// What the Tempo chip says about the selected sample: what tempo sync does to
// it, and why when it does nothing. sync.hostBpm is the tempo in force (the
// host's, Link's or the manual one); failed: the file could not be opened.
ChipText tempoChip(const audio::SampleInfo& info, const audio::SyncSettings& sync, bool failed);

// A tempo or ratio as the UI writes it, whatever the locale: tenths, with a
// whole number left whole ("120", "97.5"); a ratio to hundredths ("1.50").
std::string bpmText(double bpm);
std::string ratioText(double ratio);
// Seconds to hundredths, with the unit: "6.80 s".
std::string secondsText(double seconds);
// A length as a person reads it: under a minute as secondsText ("7.38 s"),
// from a minute up as a clock in whole seconds ("5:01", "1:02:05").
std::string lengthText(double seconds);

} // namespace asma::app
