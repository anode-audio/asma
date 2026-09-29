// SPDX-License-Identifier: GPL-3.0-only
// A child process for Subprocess and ScanSupervisor tests.
//   lines A B...  each argument on its own line
//   partial X     X with no line ending
//   crlf X        X ending in \r\n
//   args A B...   each argument as a JSON string, one per line
//   exit N        exit with code N
//   crash         die the way a real crash does
//   hang          print "ready", then sleep for a minute
// Started with --db first, it stands in for asma-scan, driven by
// ASMA_FAKE_SCAN ("key=value;..."):
//   files=a,b,c        the root's files, indexed then analysed in this order
//   crash_index=b      crash when indexing b, unless --fail b was given
//   crash_analyse=c    crash when analysing c, unless --fail-analysis c
//   crash_start=1      crash before doing anything
//   error=locked|failed report that error and exit
//   hang=1             sleep for a minute after the first start event
// Each run appends its arguments, one line, to ASMA_FAKE_SCAN_LOG.
#include "Args.h"

#include "asma/core/Json.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

[[noreturn]] void crash()
{
    std::cout << std::flush;
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX); // no crash dialog on CI
    RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    std::abort();
}

namespace {

std::vector<std::string> split(const std::string& text, char separator)
{
    std::vector<std::string> out;
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, separator))
        if (!item.empty()) out.push_back(item);
    return out;
}

void emit(const asma::JsonLine& line) { std::cout << line.build() << "\n" << std::flush; }

bool contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

// One phase: start events in groups of `threads`, then progress for each,
// crashing on `crashOn` unless it was marked.
void phase(const std::vector<std::string>& files, const std::vector<std::string>& marked, const std::string& crashOn,
           unsigned threads, const char* startEvent, const char* progressEvent, bool hang)
{
    std::vector<std::string> todo;
    for (const auto& f : files)
        if (!contains(marked, f)) todo.push_back(f);
    std::size_t done = 0;
    for (std::size_t i = 0; i < todo.size(); i += threads) {
        const std::size_t end = std::min(todo.size(), i + threads);
        for (std::size_t j = i; j < end; ++j) emit(asma::JsonLine().str("event", startEvent).str("path", todo[j]));
        if (hang) std::this_thread::sleep_for(std::chrono::seconds(60));
        for (std::size_t j = i; j < end; ++j) {
            if (todo[j] == crashOn) crash();
            emit(asma::JsonLine()
                     .str("event", progressEvent)
                     .num("done", static_cast<std::int64_t>(++done))
                     .num("total", static_cast<std::int64_t>(todo.size()))
                     .str("path", todo[j]));
        }
    }
}

int fakeScan(asma::cli::Args& args, const std::vector<std::string>& raw)
{
    if (const char* log = std::getenv("ASMA_FAKE_SCAN_LOG")) {
        std::ofstream out(log, std::ios::app);
        for (std::size_t i = 0; i < raw.size(); ++i) out << (i ? " " : "") << raw[i];
        out << "\n";
    }
    std::map<std::string, std::string> script;
    if (const char* text = std::getenv("ASMA_FAKE_SCAN"))
        for (const auto& pair : split(text, ';'))
            if (const auto eq = pair.find('='); eq != std::string::npos) script[pair.substr(0, eq)] = pair.substr(eq + 1);

    args.option("db");
    args.option("root");
    const auto threadsOption = args.option("threads");
    const unsigned threads = threadsOption ? static_cast<unsigned>(std::stoul(*threadsOption)) : 4u;
    const auto fail = args.options("fail");
    const auto failAnalysis = args.options("fail-analysis");
    const bool analyse = !args.flag("no-analysis");

    if (script["crash_start"] == "1") crash();
    if (script["error"] == "locked") {
        emit(asma::JsonLine().str("event", "error").str("code", "locked").num("pid", 4242));
        return 3;
    }
    if (script["error"] == "failed") {
        emit(asma::JsonLine().str("event", "error").str("code", "failed").str("message", "disk full"));
        return 1;
    }
    for (const auto& f : fail) emit(asma::JsonLine().str("event", "marked_failed").str("path", f));
    for (const auto& f : failAnalysis) emit(asma::JsonLine().str("event", "marked_analysis_failed").str("path", f));

    const auto files = split(script["files"], ',');
    const bool hang = script["hang"] == "1";
    phase(files, fail, script["crash_index"], threads, "start", "progress", hang);
    emit(asma::JsonLine()
             .str("event", "done")
             .num("added", static_cast<std::int64_t>(files.size() - fail.size()))
             .num("failed", static_cast<std::int64_t>(fail.size())));
    if (!analyse) return 0;
    std::vector<std::string> indexed;
    for (const auto& f : files)
        if (!contains(fail, f)) indexed.push_back(f);
    phase(indexed, failAnalysis, script["crash_analyse"], threads, "analyse_start", "analyse_progress", false);
    emit(asma::JsonLine()
             .str("event", "analyse_done")
             .num("analysed", static_cast<std::int64_t>(indexed.size() - failAnalysis.size())));
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    asma::cli::Args parsed = asma::cli::Args::fromMain(argc, argv);
    std::vector<std::string> args = parsed.rest();
    if (args.empty()) return 2;
    if (args.front() == "--db") return fakeScan(parsed, args);
    const std::string mode = args.front();
    args.erase(args.begin());

    if (mode == "lines")
        for (const auto& a : args) std::cout << a << "\n";
    else if (mode == "partial")
        std::cout << args.at(0);
    else if (mode == "crlf")
        std::cout << args.at(0) << "\r\n";
    else if (mode == "args")
        for (const auto& a : args) std::cout << '"' << asma::jsonEscape(a) << "\"\n";
    else if (mode == "exit")
        return std::stoi(args.at(0));
    else if (mode == "crash")
        crash();
    else if (mode == "hang") {
        std::cout << "ready" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(60));
    } else
        return 2;
    return 0;
}
