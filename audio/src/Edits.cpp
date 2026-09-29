// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Edits.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

PlayOptions toPlayOptions(const Edits& edits, int sourceRate, bool isLoop)
{
    PlayOptions o;
    o.trimStart = std::llround(std::max(0.0, edits.trimStart) * sourceRate);
    o.trimEnd = edits.trimEnd < 0.0 ? -1 : std::llround(edits.trimEnd * sourceRate);
    o.direction = edits.direction;
    o.loop = edits.loop == LoopMode::On || (edits.loop == LoopMode::Auto && isLoop);
    return o;
}

} // namespace asma::audio
