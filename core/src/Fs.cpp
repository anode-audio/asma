// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Fs.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <cstdlib>
#include <cstring>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#include <cwchar>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/syscall.h>
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

namespace asma {

std::string fileIdentity(const std::filesystem::path& path)
{
#ifdef _WIN32
    (void)path;
    return {};
#else
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) return {};
    return std::to_string(static_cast<unsigned long long>(st.st_dev)) + ":"
         + std::to_string(static_cast<unsigned long long>(st.st_ino));
#endif
}

std::error_code renameNoReplace(const fs::path& from, const fs::path& to)
{
#ifdef _WIN32
    // Without MOVEFILE_REPLACE_EXISTING, Windows never replaces.
    if (MoveFileExW(from.c_str(), to.c_str(), 0)) return {};
    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS) return std::make_error_code(std::errc::file_exists);
    return std::error_code(static_cast<int>(error), std::system_category());
#else
    int result = -1;
#if defined(__APPLE__)
    result = ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
    result = static_cast<int>(::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), 1 /* RENAME_NOREPLACE */));
    if (result != 0 && (errno == EINVAL || errno == ENOSYS)) {
        // A file system without it: link then unlink never replaces either.
        if (::link(from.c_str(), to.c_str()) == 0) {
            ::unlink(from.c_str());
            return {};
        }
        if (errno == EEXIST) return std::make_error_code(std::errc::file_exists);
        // Nor links (FAT, some network shares): check, then rename.
        struct stat st {};
        if (::lstat(to.c_str(), &st) == 0) return std::make_error_code(std::errc::file_exists);
        result = ::rename(from.c_str(), to.c_str());
    }
#else
    struct stat st {};
    if (::lstat(to.c_str(), &st) == 0) return std::make_error_code(std::errc::file_exists);
    result = ::rename(from.c_str(), to.c_str());
#endif
    if (result == 0) return {};
    if (errno == EEXIST) return std::make_error_code(std::errc::file_exists);
    return std::error_code(errno, std::generic_category());
#endif
}

bool sameVolume(const fs::path& a, const fs::path& b)
{
#ifdef _WIN32
    wchar_t va[MAX_PATH + 1] = {}, vb[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(fs::absolute(a).c_str(), va, MAX_PATH)) return false;
    if (!GetVolumePathNameW(fs::absolute(b).c_str(), vb, MAX_PATH)) return false;
    return _wcsicmp(va, vb) == 0;
#else
    struct stat sa {}, sb {};
    return ::stat(a.c_str(), &sa) == 0 && ::stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
#endif
}

} // namespace asma
