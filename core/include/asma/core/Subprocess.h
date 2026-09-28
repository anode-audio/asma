// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace asma {

class SubprocessError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ExitStatus {
    // Killed by a signal (POSIX) or ended by an unhandled exception
    // (Windows NTSTATUS 0xC0000000 and up). Do not rely on it alone to spot a
    // crash: abort() exits with code 3 on Windows.
    bool signalled = false;
    int code = 0; // exit code, or the signal number when signalled
};

// A child process whose stdout is read line by line. stdin and stderr are the
// null device (a plugin host may have closed its own stderr), and no other
// handle of this process leaks into the child, so several instances can run
// side by side in one host. On Windows the child gets no console window.
class Subprocess {
public:
    // Throws SubprocessError when the program cannot be started.
    static Subprocess start(const std::filesystem::path& program, const std::vector<std::string>& args);

    ~Subprocess(); // kills and reaps a child that is still running
    Subprocess(Subprocess&& other) noexcept;
    Subprocess& operator=(Subprocess&& other) noexcept;
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;

    // The next line without its line ending; nothing once stdout is closed.
    // A final line with no newline is still returned.
    std::optional<std::string> readLine();

    // Blocks until the child exits; later calls return the same status.
    ExitStatus wait();

    // Ends the child at once. Safe to call from another thread while
    // readLine() or wait() blocks, and after the child has exited.
    void kill();

private:
    Subprocess() = default;
    void close() noexcept;
    bool fill(); // reads more of stdout into buffer_; false at end of stream

#ifdef _WIN32
    void* process_ = nullptr;
    void* stdout_ = nullptr;
#else
    int pid_ = -1;
    int stdout_ = -1;
#endif
    std::string buffer_;
    bool eof_ = false;
    std::optional<ExitStatus> status_;
    std::unique_ptr<std::mutex> mutex_ = std::make_unique<std::mutex>(); // guards pid/process against kill()
};

} // namespace asma
