// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace asma {

// Only one process writes to a library at a time: the scanner or the
// standalone app's file operations. The lock is an OS lock (flock on POSIX,
// LockFileEx on Windows) on <dir>/writer.lock, held for the life of the
// object; the OS drops it if the process dies. The file also records the
// owner's PID, for messages only.
class WriterLock {
public:
    // Returns nullopt while another holder has the lock.
    static std::optional<WriterLock> tryAcquire(const std::filesystem::path& dir);
    static std::optional<std::int64_t> holder(const std::filesystem::path& dir);
    static bool processAlive(std::int64_t pid);
    static std::int64_t currentProcessId();

    ~WriterLock();
    WriterLock(WriterLock&& other) noexcept;
    WriterLock& operator=(WriterLock&& other) noexcept;
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;

private:
#ifdef _WIN32
    using Handle = void*;
    static constexpr Handle kInvalid = nullptr;
#else
    using Handle = int;
    static constexpr Handle kInvalid = -1;
#endif
    explicit WriterLock(Handle handle) : handle_(handle) {}
    void release() noexcept;

    Handle handle_ = kInvalid;
};

} // namespace asma
