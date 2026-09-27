// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <vector>

namespace asma {

struct DecodedAudio {
    int sampleRate = 0;
    std::vector<float> mono; // channel average, -1..1
    bool truncated = false;  // stopped at maxSeconds before the end
    double seconds() const { return sampleRate > 0 ? static_cast<double>(mono.size()) / sampleRate : 0.0; }
};

// Decodes up to maxSeconds from the start of the file, downmixed to mono.
// The header decides the format, as in probeFile. Throws FileAccessError when
// the file cannot be read and ProbeError when it cannot be decoded.
DecodedAudio decodeFile(const std::filesystem::path& path, double maxSeconds = 30.0);

} // namespace asma
