// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/ScanSupervisor.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace asma::app {

// The standalone's library writes: adding a sample folder and scanning it
// with asma-scan, which runs outside this process, on a background thread.
// Control calls from the message thread.
class ScanJob {
public:
    ScanJob(std::filesystem::path dbPath, std::filesystem::path worker);
    ~ScanJob(); // cancels a running scan and waits for it
    ScanJob(const ScanJob&) = delete;
    ScanJob& operator=(const ScanJob&) = delete;

    // Adds the folder to the library (creating the library on first use) and
    // starts scanning it. False, with the reason in `error`, while another
    // scan runs or when the folder cannot be added.
    bool addAndScan(const std::filesystem::path& folder, std::string* error = nullptr);
    bool busy() const { return busy_.load(); }
    // A line for the status bar while a scan runs; empty otherwise.
    std::string progress() const;
    // The finished scan's report, once.
    std::optional<ScanReport> takeReport();
    void cancel() { supervisor_.cancel(); }
    // Where asma-scan is; takes effect from the next scan.
    void setWorker(std::filesystem::path worker)
    {
        const std::lock_guard lock(mutex_);
        worker_ = std::move(worker);
    }

    // Where the app ships asma-scan: beside its own executable.
    static std::filesystem::path workerNextTo(const std::filesystem::path& executable);

private:
    std::filesystem::path dbPath_;
    std::filesystem::path worker_;
    ScanSupervisor supervisor_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    mutable std::mutex mutex_; // guards progress_ and report_
    std::string progress_;
    std::optional<ScanReport> report_;
};

} // namespace asma::app
