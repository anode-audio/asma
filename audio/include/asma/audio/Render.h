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
    bool changesAudio() const
    {
        // Ordered comparisons: the plugin build warns on float == and !=.
        return edits.changesAudio() || ratio < 1.0 || ratio > 1.0 || semitones < 0.0 || semitones > 0.0;
    }
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

// Rendered drag-out files, <dir>/<content hash>-<settings>.wav. Nothing is
// deleted behind the user's back: a DAW may play a dragged file from where it
// lies, so a render stays until clear(). Not thread-safe.
class RenderStore {
public:
    explicit RenderStore(std::filesystem::path dir) : dir_(std::move(dir)) {}

    // <data dir>/renders: user data, so cleaner apps that empty caches leave it.
    static std::filesystem::path defaultDir();

    // The file to drag: `source` itself when nothing changes the audio,
    // else a render, made on a miss. contentHash is the library's; empty
    // means hash the file here.
    std::filesystem::path fileFor(const std::filesystem::path& source, const RenderSettings& settings,
                                  std::string contentHash = {});

    // The name a render gets. Integers only, so no locale can change it.
    static std::string fileName(std::string_view contentHash, const RenderSettings& settings, int sampleRate);

    const std::filesystem::path& dir() const { return dir_; }
    // Bytes on disk, temporary files left by an interrupted render included.
    std::uintmax_t bytes() const;
    // Deletes every render and leftover temporary file; returns how many.
    std::size_t clear();

private:
    std::filesystem::path dir_;
};

} // namespace asma::audio
