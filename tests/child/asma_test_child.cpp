// SPDX-License-Identifier: GPL-3.0-only
// A child process for Subprocess and ScanSupervisor tests.
//   lines A B...  each argument on its own line
//   partial X     X with no line ending
//   crlf X        X ending in \r\n
//   args A B...   each argument as a JSON string, one per line
//   exit N        exit with code N
//   crash         die the way a real crash does
//   hang          print "ready", then sleep for a minute
#include "Args.h"

#include "asma/core/Json.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

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

int main(int argc, char** argv)
{
    asma::cli::Args parsed = asma::cli::Args::fromMain(argc, argv);
    std::vector<std::string> args = parsed.rest();
    if (args.empty()) return 2;
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
