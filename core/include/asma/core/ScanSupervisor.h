// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/ScanEvents.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace asma {

class Subprocess;

struct ScanRequest {
    std::filesystem::path worker; // the asma-scan executable
    std::filesystem::path db;
    std::int64_t rootId = 0;
    unsigned threads = 0; // 0: the worker's default
    bool analyse = true;
};

struct ScanReport {
    enum class Result { Finished, Locked, Failed, Crashed, Cancelled };
    Result result = Result::Finished;
    std::string message;                   // Failed: the worker's message; Crashed: what went wrong
    std::optional<std::int64_t> lockHolder; // Locked: the pid holding the writer lock, when known
    ScanStats index;                       // from the last run, so earlier runs' additions count as unchanged
    AnalyseStats analysis;
    std::vector<std::pair<ScanPhase, std::string>> culprits; // files marked for crashing the worker
    int runs = 0;
};

// Runs asma-scan for one root and sees it through crashes, following the
// contract in docs/scan-protocol.md. The UI runs it on a background thread.
class ScanSupervisor {
public:
    // Called on the thread that runs run(), for every event of every run.
    using Listener = std::function<void(const ScanEvent&)>;

    ScanReport run(const ScanRequest& request, const Listener& listener = {});

    // Stops the scan in progress: kills the worker and makes run() return
    // Cancelled. Safe from any thread. A call while no run() is active does
    // nothing, so a Cancel pressed as a scan ends never stops the next one.
    void cancel();

private:
    std::atomic<bool> cancelled_{false};
    std::mutex mutex_;
    Subprocess* current_ = nullptr; // guarded by mutex_
};

// The worker's arguments for one attempt.
std::vector<std::string> scanArguments(const ScanRequest& request, const ScanAttempt& attempt);

} // namespace asma
