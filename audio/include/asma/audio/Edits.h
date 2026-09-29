// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/PlayHead.h"

namespace asma::audio {

enum class LoopMode { Auto, On, Off }; // Auto: loops loop, one-shots play once

// What the user did to a sample, in the UI's units.
struct Edits {
    double trimStart = 0.0; // seconds
    double trimEnd = -1.0;  // seconds; negative means the end of the file
    Direction direction = Direction::Forward;
    LoopMode loop = LoopMode::Auto;

    // Trim and direction are what a render has to bake in; looping is not.
    bool changesAudio() const { return trimStart > 0.0 || trimEnd >= 0.0 || direction != Direction::Forward; }
};

PlayOptions toPlayOptions(const Edits& edits, int sourceRate, bool isLoop);

} // namespace asma::audio
