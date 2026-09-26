// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace asma {

// UTF-8 text of a path with '/' separators on every platform. This is the only
// form in which paths are stored in the database.
std::string toUtf8(const std::filesystem::path& path);

// Inverse of toUtf8.
std::filesystem::path fromUtf8(std::string_view utf8);

// Binary-mode fopen for reading that handles non-ASCII paths on Windows.
// Returns nullptr on failure.
std::FILE* openFileRead(const std::filesystem::path& path);

// 64-bit safe absolute seek. Returns false on failure.
bool seekFile(std::FILE* file, std::uint64_t offset);

struct FileCloser {
    void operator()(std::FILE* file) const noexcept
    {
        if (file) std::fclose(file);
    }
};
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

// Per-user data directory. ASMA_DATA_DIR wins when set; otherwise
// macOS: ~/Library/Application Support/Anode Labs/asma
// Windows: %APPDATA%\Anode Labs\asma
// Linux: $XDG_DATA_HOME/anode-labs/asma, else ~/.local/share/anode-labs/asma
// The directory is not created.
std::filesystem::path defaultDataDir();

// Last-write time as an opaque integer in the file clock's native ticks. Only
// meaningful for equality and ordering on the same machine.
std::int64_t fileTimeToInt(std::filesystem::file_time_type time);

} // namespace asma
