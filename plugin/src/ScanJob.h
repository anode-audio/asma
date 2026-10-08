// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/ScanSupervisor.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace asma::app {

// What runs a folder's scan, one at a time; the keeper drives it, and tests
// give the keeper a fake one. Control calls from the message thread.
class ScanRunner {
public:
    virtual ~ScanRunner() = default;
    // Starts scanning a folder of the library; `label` names it in progress
    // lines. False while a scan runs.
    virtual bool start(std::int64_t rootId, const std::string& label) = 0;
    virtual bool busy() const = 0;
    // "Scanning Samples: 1,200 of 8,000" while a scan runs; empty otherwise.
    virtual std::string progress() const = 0;
    // The finished scan's report, once.
    virtual std::optional<ScanReport> takeReport() = 0;
    // Whether it can scan at all (asma-scan is there).
    virtual bool ready() const = 0;
};

// Scans with asma-scan, which runs outside this process, on a background
// thread: the app's and the plugins' scans, so a plugin never writes the
// library inside its host.
class ScanJob final : public ScanRunner {
public:
    ScanJob(std::filesystem::path dbPath, std::filesystem::path worker);
    ~ScanJob() override; // cancels a running scan and waits for it
    ScanJob(const ScanJob&) = delete;
    ScanJob& operator=(const ScanJob&) = delete;

    bool start(std::int64_t rootId, const std::string& label) override;
    bool busy() const override { return busy_.load(); }
    std::string progress() const override;
    std::optional<ScanReport> takeReport() override;
    bool ready() const override;

    // Adds the folder to the library (creating the library on first use) and
    // starts scanning it: the standalone only. False, with the reason in
    // `error`, while another scan runs or when the folder cannot be added.
    bool addAndScan(const std::filesystem::path& folder, std::string* error = nullptr);
    void cancel() { supervisor_.cancel(); }
    // Where asma-scan is; takes effect from the next scan.
    void setWorker(std::filesystem::path worker)
    {
        const std::lock_guard lock(mutex_);
        worker_ = std::move(worker);
    }

    // Where the app and the plugins ship asma-scan: beside their own binary.
    static std::filesystem::path workerNextTo(const std::filesystem::path& executable);

private:
    std::filesystem::path dbPath_;
    std::filesystem::path worker_;
    ScanSupervisor supervisor_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    mutable std::mutex mutex_; // guards worker_, progress_ and report_
    std::string progress_;
    std::optional<ScanReport> report_;
};

// 8000 as "8,000": integer arithmetic, never the locale.
std::string groupDigits(std::int64_t n);

} // namespace asma::app
