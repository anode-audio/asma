// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/audio/SampleSource.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace asma {
class AudioReader;
}

namespace asma::audio {

// Recently decoded files, least recently used out first. A file edited on
// disk (other size or mtime) is decoded again. Thread-safe; decoding happens
// outside the lock.
class PreviewCache {
public:
    static constexpr std::size_t kDefaultCapacity = 256u << 20; // bytes of samples

    explicit PreviewCache(std::size_t capacityBytes = kDefaultCapacity) : capacity_(capacityBytes) {}

    // The cached buffer when the file has not changed since, else nullptr.
    // Throws FileAccessError when the file is gone.
    std::shared_ptr<const AudioBuffer> find(const std::filesystem::path& path);
    // Decodes through `reader`, freshly opened on `path`, and keeps the result.
    // Throws ProbeError.
    std::shared_ptr<const AudioBuffer> load(const std::filesystem::path& path, AudioReader& reader);
    // find, or open and load. Throws ProbeError (FileAccessError when the
    // file is gone).
    std::shared_ptr<const AudioBuffer> get(const std::filesystem::path& path);

    std::size_t bytes() const;
    std::size_t size() const;

private:
    struct Stamp {
        std::int64_t size = 0;
        std::int64_t mtime = 0;
        bool operator==(const Stamp&) const = default;
    };
    struct Entry {
        std::string key;
        Stamp stamp;
        std::shared_ptr<const AudioBuffer> buffer;
    };

    static Stamp stampOf(const std::filesystem::path& path);

    mutable std::mutex mutex_;
    std::size_t capacity_;
    std::size_t bytes_ = 0;
    std::list<Entry> entries_; // most recent first
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

} // namespace asma::audio
