// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/Edits.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace asma::audio {

// What a drag-out bakes into the file.
struct RenderSettings {
    Edits edits;           // trim and direction; looping is ignored: one pass
    double ratio = 1.0;    // tempo, as SyncPlan::ratio
    double semitones = 0.0;
    int sampleRate = 0;    // the host's; 0 keeps the file's own

    // Without these the original file is what gets dragged. A sample-rate
    // change alone is not an edit: the DAW converts on import.
    bool changesAudio() const { return edits.changesAudio() || ratio != 1.0 || semitones != 0.0; }
};

// Renders one pass of `source` with the edits baked in, as a 32-bit float WAV
// with the source's channel count (up to two). The length is exact: the
// trimmed pass divided by the ratio, so a synced loop lands on the grid.
// Writes to a temporary name and renames, so `out` never holds half a file.
// Throws ProbeError when the source cannot be read, std::invalid_argument for
// a trim that leaves nothing or a sample rate outside 1 kHz..768 kHz, and
// std::runtime_error when the output cannot be written.
void renderToFile(const std::filesystem::path& source, const RenderSettings& settings,
                  const std::filesystem::path& out);

// Rendered drag-out files, <dir>/<content hash>-<settings>.wav, least
// recently used out first once they pass the capacity. Not thread-safe.
class RenderCache {
public:
    static constexpr std::uintmax_t kDefaultCapacity = 2ull << 30; // 2 GB

    explicit RenderCache(std::filesystem::path dir, std::uintmax_t capacity = kDefaultCapacity)
        : dir_(std::move(dir)), capacity_(capacity)
    {
    }

    // The file to drag: `source` itself when nothing changes the audio,
    // else a render, made on a miss. contentHash is the library's; empty
    // means hash the file here.
    std::filesystem::path fileFor(const std::filesystem::path& source, const RenderSettings& settings,
                                  std::string contentHash = {});

    // The name a render gets. Integers only, so no locale can change it.
    static std::string fileName(std::string_view contentHash, const RenderSettings& settings, int sampleRate);

    const std::filesystem::path& dir() const { return dir_; }

private:
    void evict(const std::filesystem::path& keep);

    std::filesystem::path dir_;
    std::uintmax_t capacity_;
};

} // namespace asma::audio
