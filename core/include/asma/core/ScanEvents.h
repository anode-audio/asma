// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Analyser.h"
#include "asma/core/Scanner.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asma {

enum class ScanPhase { Index, Analyse };

// One line of asma-scan output (docs/scan-protocol.md).
struct ScanEvent {
    enum class Kind {
        MarkedFailed,
        MarkedAnalysisFailed,
        Start,
        Progress,
        Done,
        AnalyseStart,
        AnalyseProgress,
        AnalyseDone,
        Error,
        Unknown, // an event this build does not know; ignore it
    };
    Kind kind = Kind::Unknown;
    std::string path;              // root-relative, for the per-file events
    std::int64_t done = 0;         // Progress, AnalyseProgress
    std::int64_t total = 0;
    ScanStats index;               // Done
    AnalyseStats analysis;         // AnalyseDone
    std::string code;              // Error: "locked" or "failed"
    std::string message;           // Error
    std::optional<std::int64_t> pid; // Error "locked": the lock holder
};

// Nothing for a line that is not a JSON object with a string "event" field.
std::optional<ScanEvent> parseScanEvent(std::string_view line);

// Arguments for one asma-scan run of a supervised scan.
struct ScanAttempt {
    unsigned threads = 0; // 0: let the worker decide
    std::vector<std::string> fail;         // --fail
    std::vector<std::string> failAnalysis; // --fail-analysis
};

// The supervisor's side of the crash-recovery contract, without processes:
// feed it every event of a run, then ask what to do when the worker exits.
//
// A worker that exits without finishing, and without reporting an error,
// crashed. With several threads the crash cannot be pinned on one file, so
// the next run uses one thread; when a one-thread run crashes, the file it
// was working on is passed back with --fail or --fail-analysis and the thread
// count goes back to what was asked for. A crash that no file explains ends
// the scan after two tries, and so does a file that crashes the worker again
// after it was marked.
class ScanRecovery {
public:
    enum class Next { Finished, Retry, Locked, Failed, Crashed };

    ScanRecovery(unsigned threads, bool analyse);

    const ScanAttempt& attempt() const { return attempt_; }
    void onEvent(const ScanEvent& event);
    // Call once the worker has exited. On Retry, attempt() holds the new
    // arguments and the per-run state starts again.
    Next onExit();

    // Files marked as crashing the worker so far, in order.
    const std::vector<std::pair<ScanPhase, std::string>>& culprits() const { return culprits_; }
    const std::optional<ScanEvent>& error() const { return error_; }

private:
    void newRun();

    unsigned requestedThreads_;
    bool analyse_;
    ScanAttempt attempt_;
    std::vector<std::pair<ScanPhase, std::string>> inFlight_; // started, not finished, in start order
    std::vector<std::pair<ScanPhase, std::string>> culprits_;
    bool indexDone_ = false;
    bool analysisDone_ = false;
    std::optional<ScanEvent> error_;
    int unexplainedCrashes_ = 0;
};

} // namespace asma
