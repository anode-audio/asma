// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/AudioProbe.h"

#include <cstdint>
#include <cstdio>

namespace asma::detail {

// Chunk walkers for RIFF/WAVE and IFF/AIFF. Throw ProbeError.
ProbeResult probeWav(std::FILE* file, std::uint64_t fileSize);
ProbeResult probeAiff(std::FILE* file, std::uint64_t fileSize);

} // namespace asma::detail
