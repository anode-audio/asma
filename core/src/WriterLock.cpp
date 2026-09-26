// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/WriterLock.h"

#include <chrono>
#include <fstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr const char* kLockName = "writer.lock";

bool createExclusive(const fs::path& file, const std::string& content)
{
#ifdef _WIN32
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(handle, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(handle);
    return true;
#else
    const int fd = ::open(file.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) return false;
    const ssize_t written = ::write(fd, content.data(), content.size());
    (void)written;
    ::close(fd);
    return true;
#endif
}

} // namespace

std::int64_t WriterLock::currentProcessId()
{
#ifdef _WIN32
    return static_cast<std::int64_t>(GetCurrentProcessId());
#else
    return static_cast<std::int64_t>(::getpid());
#endif
}

bool WriterLock::processAlive(std::int64_t pid)
{
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return ok && code == STILL_ACTIVE;
#else
    if (pid > std::int64_t{0x7fffffff}) return false;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

std::optional<std::int64_t> WriterLock::holder(const fs::path& dir)
{
    std::ifstream in(dir / kLockName);
    std::int64_t pid = 0;
    if (in >> pid) return pid;
    return std::nullopt;
}

std::optional<WriterLock> WriterLock::tryAcquire(const fs::path& dir)
{
    fs::create_directories(dir);
    const fs::path file = dir / kLockName;
    const std::string content = std::to_string(currentProcessId());

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (createExclusive(file, content)) return WriterLock(file);

        if (const auto pid = holder(dir)) {
            if (processAlive(*pid)) return std::nullopt;
        } else {
            // No readable PID: the owner may be between create and write.
            std::error_code ec;
            const auto written = fs::last_write_time(file, ec);
            if (!ec && fs::file_time_type::clock::now() - written < std::chrono::seconds(5)) return std::nullopt;
        }
        std::error_code ec;
        fs::remove(file, ec); // stale: take it over on the next attempt
    }
    return std::nullopt;
}

void WriterLock::release() noexcept
{
    if (file_.empty()) return;
    try {
        if (holder(file_.parent_path()) == currentProcessId()) {
            std::error_code ec;
            fs::remove(file_, ec);
        }
    } catch (...) {
    }
    file_.clear();
}

WriterLock::~WriterLock() { release(); }

WriterLock::WriterLock(WriterLock&& other) noexcept : file_(std::exchange(other.file_, {})) {}

WriterLock& WriterLock::operator=(WriterLock&& other) noexcept
{
    if (this != &other) {
        release();
        file_ = std::exchange(other.file_, {});
    }
    return *this;
}

} // namespace asma
