// SPDX-License-Identifier: GPL-3.0-only
#include "asma/audio/PreviewCache.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/AudioReader.h"
#include "asma/core/Fs.h"

namespace fs = std::filesystem;

namespace asma::audio {

PreviewCache::Stamp PreviewCache::stampOf(const fs::path& path)
{
    std::error_code ec;
    Stamp stamp;
    stamp.size = static_cast<std::int64_t>(fs::file_size(path, ec));
    if (ec) throw FileAccessError("cannot open file");
    stamp.mtime = fileTimeToInt(fs::last_write_time(path, ec));
    if (ec) throw FileAccessError("cannot open file");
    return stamp;
}

std::shared_ptr<const AudioBuffer> PreviewCache::find(const fs::path& path)
{
    const Stamp stamp = stampOf(path);
    const std::string key = toUtf8(path);
    const std::lock_guard lock(mutex_);
    const auto it = index_.find(key);
    if (it == index_.end()) return nullptr;
    if (it->second->stamp == stamp) {
        entries_.splice(entries_.begin(), entries_, it->second);
        return it->second->buffer;
    }
    bytes_ -= it->second->buffer->bytes();
    entries_.erase(it->second);
    index_.erase(it);
    return nullptr;
}

std::shared_ptr<const AudioBuffer> PreviewCache::load(const fs::path& path, AudioReader& reader)
{
    // Stamp first: a file edited while it decodes is decoded again next time.
    const Stamp stamp = stampOf(path);
    auto buffer = std::make_shared<const AudioBuffer>(loadAudio(reader));
    const std::string key = toUtf8(path);
    const std::lock_guard lock(mutex_);
    if (buffer->bytes() > capacity_) return buffer; // too big to keep
    if (const auto it = index_.find(key); it != index_.end()) {
        bytes_ -= it->second->buffer->bytes();
        entries_.erase(it->second);
        index_.erase(it);
    }
    entries_.push_front({key, stamp, buffer});
    index_[key] = entries_.begin();
    bytes_ += buffer->bytes();
    while (bytes_ > capacity_) {
        bytes_ -= entries_.back().buffer->bytes();
        index_.erase(entries_.back().key);
        entries_.pop_back();
    }
    return buffer;
}

std::shared_ptr<const AudioBuffer> PreviewCache::get(const fs::path& path)
{
    if (auto hit = find(path)) return hit;
    return load(path, *AudioReader::open(path));
}

std::size_t PreviewCache::bytes() const
{
    const std::lock_guard lock(mutex_);
    return bytes_;
}

std::size_t PreviewCache::size() const
{
    const std::lock_guard lock(mutex_);
    return entries_.size();
}

} // namespace asma::audio
