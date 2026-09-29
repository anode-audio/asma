// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleInfo.h"

namespace asma::audio {

constexpr double kTargetLufs = -16.0;
constexpr double kMaxBoostDb = 12.0;
constexpr double kPeakCeiling = 0.891; // -1 dBFS

// The linear gain that brings a sample to kTargetLufs, so browsing does not
// jump in level from file to file. A boost is at most kMaxBoostDb and never
// takes the sample peak past kPeakCeiling; cuts are not limited. 1 when
// loudness is unknown.
float matchGain(const SampleInfo& info);

} // namespace asma::audio
