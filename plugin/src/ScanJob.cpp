// SPDX-License-Identifier: GPL-3.0-only
#include "ScanJob.h"

#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"

namespace asma::app {

ScanJob::ScanJob(std::filesystem::path dbPath, std::filesystem::path worker)
    : dbPath_(std::move(dbPath)), worker_(std::move(worker))
{
}

ScanJob::~ScanJob()
{
    supervisor_.cancel();
    if (thread_.joinable()) thread_.join();
}

bool ScanJob::addAndScan(const std::filesystem::path& folder, std::string* error)
{
    const auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (busy_.load()) return fail("a scan is already running");
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return fail("not a folder");
    std::int64_t rootId = 0;
    try {
        Db db = Db::open(dbPath_);
        rootId = Library(db).addRoot(folder);
    } catch (const std::exception& e) {
        return fail(e.what());
    }
    return start(rootId, toUtf8(folder.filename())) || fail("a scan is already running");
}

bool ScanJob::start(std::int64_t rootId, const std::string& label)
{
    if (busy_.load()) return false;
    if (thread_.joinable()) thread_.join(); // the previous scan, already finished
    busy_.store(true);
    std::filesystem::path worker;
    {
        const std::lock_guard lock(mutex_);
        progress_ = "Scanning " + label;
        report_.reset();
        worker = worker_;
    }
    thread_ = std::thread([this, rootId, worker, label] {
        ScanRequest request;
        request.worker = worker;
        request.db = dbPath_;
        request.rootId = rootId;
        const ScanReport report = supervisor_.run(request, [this, label](const ScanEvent& e) {
            const char* what = e.kind == ScanEvent::Kind::Progress          ? "Scanning"
                             : e.kind == ScanEvent::Kind::AnalyseProgress ? "Analysing"
                                                                            : nullptr;
            if (!what) return;
            const std::lock_guard lock(mutex_);
            progress_ = std::string(what) + " " + label + ": " + groupDigits(static_cast<std::int64_t>(e.done)) + " of "
                      + groupDigits(static_cast<std::int64_t>(e.total));
        });
        const std::lock_guard lock(mutex_);
        report_ = report;
        progress_.clear();
        busy_.store(false);
    });
    return true;
}

bool ScanJob::ready() const
{
    std::filesystem::path worker;
    {
        const std::lock_guard lock(mutex_);
        worker = worker_;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(worker, ec);
}

std::string groupDigits(std::int64_t n)
{
    std::string digits = std::to_string(n < 0 ? -n : n);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<std::size_t>(i), ",");
    return (n < 0 ? "-" : "") + digits;
}

std::string ScanJob::progress() const
{
    const std::lock_guard lock(mutex_);
    return progress_;
}

std::optional<ScanReport> ScanJob::takeReport()
{
    const std::lock_guard lock(mutex_);
    std::optional<ScanReport> report;
    report.swap(report_);
    return report;
}

std::filesystem::path ScanJob::workerNextTo(const std::filesystem::path& executable)
{
#ifdef _WIN32
    return executable.parent_path() / "asma-scan.exe";
#else
    return executable.parent_path() / "asma-scan";
#endif
}

} // namespace asma::app
