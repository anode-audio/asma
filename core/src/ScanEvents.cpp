// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ScanEvents.h"

#include "asma/core/Json.h"

#include <algorithm>

namespace asma {

namespace {

std::string text(const JsonValue& doc, std::string_view key)
{
    const JsonValue* v = doc.get(key);
    return v && v->asString() ? *v->asString() : std::string();
}

std::int64_t integer(const JsonValue& doc, std::string_view key)
{
    const JsonValue* v = doc.get(key);
    return v ? v->asInt().value_or(0) : 0;
}

std::size_t count(const JsonValue& doc, std::string_view key)
{
    return static_cast<std::size_t>(std::max<std::int64_t>(0, integer(doc, key)));
}

} // namespace

std::optional<ScanEvent> parseScanEvent(std::string_view line)
{
    JsonValue doc;
    try {
        doc = parseJson(line);
    } catch (const JsonError&) {
        return std::nullopt;
    }
    const JsonValue* name = doc.get("event");
    if (!name || !name->asString()) return std::nullopt;

    using Kind = ScanEvent::Kind;
    static constexpr std::pair<std::string_view, Kind> kKinds[] = {
        {"marked_failed", Kind::MarkedFailed},
        {"marked_analysis_failed", Kind::MarkedAnalysisFailed},
        {"start", Kind::Start},
        {"progress", Kind::Progress},
        {"done", Kind::Done},
        {"analyse_start", Kind::AnalyseStart},
        {"analyse_progress", Kind::AnalyseProgress},
        {"analyse_done", Kind::AnalyseDone},
        {"error", Kind::Error},
    };
    ScanEvent e;
    for (const auto& [n, kind] : kKinds)
        if (n == *name->asString()) e.kind = kind;

    e.path = text(doc, "path");
    e.done = integer(doc, "done");
    e.total = integer(doc, "total");
    if (e.kind == Kind::Done) {
        e.index.added = count(doc, "added");
        e.index.updated = count(doc, "updated");
        e.index.unchanged = count(doc, "unchanged");
        e.index.relinked = count(doc, "relinked");
        e.index.missing = count(doc, "missing");
        e.index.failed = count(doc, "failed");
        e.index.skipped = count(doc, "skipped");
    }
    if (e.kind == Kind::AnalyseDone) {
        e.analysis.analysed = count(doc, "analysed");
        e.analysis.failed = count(doc, "failed");
        e.analysis.skipped = count(doc, "skipped");
    }
    e.code = text(doc, "code");
    e.message = text(doc, "message");
    if (const JsonValue* pid = doc.get("pid")) e.pid = pid->asInt();
    return e;
}

ScanRecovery::ScanRecovery(unsigned threads, bool analyse) : requestedThreads_(threads), analyse_(analyse)
{
    attempt_.threads = threads;
}

void ScanRecovery::newRun()
{
    inFlight_.clear();
    indexDone_ = false;
    analysisDone_ = false;
    error_.reset();
}

void ScanRecovery::onEvent(const ScanEvent& event)
{
    using Kind = ScanEvent::Kind;
    const auto finish = [&](ScanPhase phase) {
        const auto it = std::find(inFlight_.begin(), inFlight_.end(), std::make_pair(phase, event.path));
        if (it != inFlight_.end()) inFlight_.erase(it);
    };
    switch (event.kind) {
    case Kind::Start: inFlight_.emplace_back(ScanPhase::Index, event.path); break;
    case Kind::AnalyseStart: inFlight_.emplace_back(ScanPhase::Analyse, event.path); break;
    case Kind::Progress: finish(ScanPhase::Index); break;
    case Kind::AnalyseProgress: finish(ScanPhase::Analyse); break;
    case Kind::Done: indexDone_ = true; break;
    case Kind::AnalyseDone: analysisDone_ = true; break;
    case Kind::Error: error_ = event; break;
    default: break;
    }
}

ScanRecovery::Next ScanRecovery::onExit()
{
    if (error_) return error_->code == "locked" ? Next::Locked : Next::Failed;
    if (indexDone_ && (analysisDone_ || !analyse_)) return Next::Finished;

    // The worker crashed.
    if (attempt_.threads != 1) {
        attempt_.threads = 1;
        newRun();
        return Next::Retry;
    }
    if (inFlight_.empty()) {
        if (++unexplainedCrashes_ >= 2) return Next::Crashed;
        newRun();
        return Next::Retry;
    }
    const auto culprit = inFlight_.back();
    if (std::find(culprits_.begin(), culprits_.end(), culprit) != culprits_.end())
        return Next::Crashed; // marking it did not help
    culprits_.push_back(culprit);
    (culprit.first == ScanPhase::Index ? attempt_.fail : attempt_.failAnalysis).push_back(culprit.second);
    attempt_.threads = requestedThreads_;
    unexplainedCrashes_ = 0;
    newRun();
    return Next::Retry;
}

} // namespace asma
