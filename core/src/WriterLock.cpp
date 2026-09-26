// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/WriterLock.h"

#include <fstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr const char* kLockName = "writer.lock";

#ifdef _WIN32
// Lock one byte far past the end of the file so the PID text stays readable
// by other processes while the lock is held.
constexpr DWORD kLockOffsetHigh = 1;
#endif

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
    const std::string pid = std::to_string(currentProcessId());

#ifdef _WIN32
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    OVERLAPPED region{};
    region.OffsetHigh = kLockOffsetHigh;
    if (!LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &region)) {
        CloseHandle(handle);
        return std::nullopt;
    }
    SetFilePointer(handle, 0, nullptr, FILE_BEGIN);
    SetEndOfFile(handle);
    DWORD written = 0;
    WriteFile(handle, pid.data(), static_cast<DWORD>(pid.size()), &written, nullptr);
    FlushFileBuffers(handle);
    return WriterLock(handle);
#else
    const int fd = ::open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) return std::nullopt;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return std::nullopt;
    }
    if (::ftruncate(fd, 0) == 0) {
        const ssize_t written = ::pwrite(fd, pid.data(), pid.size(), 0);
        (void)written;
    }
    return WriterLock(fd);
#endif
}

void WriterLock::release() noexcept
{
    if (handle_ == kInvalid) return;
    // The file stays: deleting it would let a newcomer lock a fresh inode while
    // a waiter still holds the old one open.
#ifdef _WIN32
    OVERLAPPED region{};
    region.OffsetHigh = kLockOffsetHigh;
    UnlockFileEx(static_cast<HANDLE>(handle_), 0, 1, 0, &region);
    CloseHandle(static_cast<HANDLE>(handle_));
#else
    ::flock(handle_, LOCK_UN);
    ::close(handle_);
#endif
    handle_ = kInvalid;
}

WriterLock::~WriterLock() { release(); }

WriterLock::WriterLock(WriterLock&& other) noexcept : handle_(std::exchange(other.handle_, kInvalid)) {}

WriterLock& WriterLock::operator=(WriterLock&& other) noexcept
{
    if (this != &other) {
        release();
        handle_ = std::exchange(other.handle_, kInvalid);
    }
    return *this;
}

} // namespace asma
