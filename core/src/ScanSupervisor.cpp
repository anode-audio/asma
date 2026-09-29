// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ScanSupervisor.h"

#include "asma/core/Fs.h"
#include "asma/core/Subprocess.h"

namespace asma {

std::vector<std::string> scanArguments(const ScanRequest& request, const ScanAttempt& attempt)
{
    std::vector<std::string> args = {"--db", toUtf8(request.db), "--root", std::to_string(request.rootId)};
    if (attempt.threads > 0) {
        args.push_back("--threads");
        args.push_back(std::to_string(attempt.threads));
    }
    if (!request.analyse) args.push_back("--no-analysis");
    for (const auto& path : attempt.fail) {
        args.push_back("--fail");
        args.push_back(path);
    }
    for (const auto& path : attempt.failAnalysis) {
        args.push_back("--fail-analysis");
        args.push_back(path);
    }
    return args;
}

void ScanSupervisor::cancel()
{
    cancelled_ = true;
    std::lock_guard lock(mutex_);
    if (current_) current_->kill();
}

ScanReport ScanSupervisor::run(const ScanRequest& request, const Listener& listener)
{
    using Result = ScanReport::Result;
    cancelled_ = false; // a Cancel from before this run was for an earlier one
    ScanReport report;
    ScanRecovery recovery(request.threads, request.analyse);

    for (;;) {
        if (cancelled_) {
            report.result = Result::Cancelled;
            break;
        }
        std::optional<Subprocess> worker;
        try {
            worker.emplace(Subprocess::start(request.worker, scanArguments(request, recovery.attempt())));
        } catch (const SubprocessError& e) {
            report.result = Result::Failed;
            report.message = e.what();
            break;
        }
        ++report.runs;
        {
            std::lock_guard lock(mutex_);
            current_ = &*worker;
        }
        if (cancelled_) worker->kill(); // cancel() came between the check above and here

        while (const auto line = worker->readLine()) {
            const auto event = parseScanEvent(*line);
            if (!event) continue;
            if (event->kind == ScanEvent::Kind::Done) report.index = event->index;
            if (event->kind == ScanEvent::Kind::AnalyseDone) report.analysis = event->analysis;
            recovery.onEvent(*event);
            if (listener) listener(*event);
        }
        worker->wait();
        {
            std::lock_guard lock(mutex_);
            current_ = nullptr;
        }
        if (cancelled_) {
            report.result = Result::Cancelled;
            break;
        }

        const auto next = recovery.onExit();
        if (next == ScanRecovery::Next::Retry) continue;
        if (next == ScanRecovery::Next::Finished) report.result = Result::Finished;
        if (next == ScanRecovery::Next::Locked) {
            report.result = Result::Locked;
            report.lockHolder = recovery.error()->pid;
        }
        if (next == ScanRecovery::Next::Failed) {
            report.result = Result::Failed;
            report.message = recovery.error()->message;
        }
        if (next == ScanRecovery::Next::Crashed) {
            report.result = Result::Crashed;
            report.message = "the scanner kept crashing";
        }
        break;
    }
    report.culprits = recovery.culprits();
    return report;
}

} // namespace asma
