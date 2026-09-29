// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/Gain.h"

#include <algorithm>
#include <cmath>

namespace asma::audio {

float matchGain(const SampleInfo& info)
{
    if (!info.lufs) return 1.0f;
    double gain = std::pow(10.0, std::min(kTargetLufs - *info.lufs, kMaxBoostDb) / 20.0);
    if (info.peak && *info.peak > 0.0) gain = std::min(gain, std::max(1.0, kPeakCeiling / *info.peak));
    return static_cast<float>(gain);
}

} // namespace asma::audio
