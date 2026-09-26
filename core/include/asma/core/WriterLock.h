// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace asma {

// Only one process writes to a library at a time: the scanner or the
// standalone app's file operations. The lock is <dir>/writer.lock holding the
// owner's PID.
class WriterLock {
public:
    // Returns nullopt while another live process holds the lock. A lock left
    // by a dead process is taken over.
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
    explicit WriterLock(std::filesystem::path file) : file_(std::move(file)) {}
    void release() noexcept;

    std::filesystem::path file_;
};

} // namespace asma
