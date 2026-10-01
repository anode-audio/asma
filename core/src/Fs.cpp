// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Fs.h"

#include <cstdlib>
#include <cstring>
#include <optional>

#ifdef _WIN32
#include <cwchar>
#else
#include <sys/types.h>
#endif

namespace fs = std::filesystem;

namespace asma {

std::string toUtf8(const fs::path& path)
{
    const std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path fromUtf8(std::string_view utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::FILE* openFileRead(const fs::path& path)
{
#ifdef _WIN32
    return _wfopen(path.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool seekFile(std::FILE* file, std::uint64_t offset)
{
#ifdef _WIN32
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

namespace {

std::optional<fs::path> envPath(const char* name)
{
#ifdef _WIN32
    const std::wstring wide(name, name + std::strlen(name));
    if (const wchar_t* value = _wgetenv(wide.c_str()); value && *value) return fs::path(value);
#else
    if (const char* value = std::getenv(name); value && *value) return fs::path(value);
#endif
    return std::nullopt;
}

} // namespace

fs::path defaultDataDir()
{
    if (auto dir = envPath("ASMA_DATA_DIR")) return *dir;
#if defined(_WIN32)
    if (auto appData = envPath("APPDATA")) return *appData / "Anode Labs" / "asma";
    return fs::temp_directory_path() / "Anode Labs" / "asma";
#elif defined(__APPLE__)
    if (auto home = envPath("HOME"))
        return *home / "Library" / "Application Support" / "Anode Labs" / "asma";
    return fs::temp_directory_path() / "Anode Labs" / "asma";
#else
    if (auto xdg = envPath("XDG_DATA_HOME")) return *xdg / "anode-labs" / "asma";
    if (auto home = envPath("HOME")) return *home / ".local" / "share" / "anode-labs" / "asma";
    return fs::temp_directory_path() / "anode-labs" / "asma";
#endif
}

std::int64_t fileTimeToInt(fs::file_time_type time)
{
    return static_cast<std::int64_t>(time.time_since_epoch().count());
}

} // namespace asma
