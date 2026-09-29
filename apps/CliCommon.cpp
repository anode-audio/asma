// SPDX-License-Identifier: GPL-3.0-only
#include "CliCommon.h"

#include "asma/core/Fs.h"

#ifdef _WIN32
#include <windows.h>

#include <cstdlib>
#endif

namespace asma::cli {

std::filesystem::path resolveDbPath(Args& args)
{
    // Absolute, so the lock directory (the parent) is never empty.
    if (auto db = args.option("db")) return std::filesystem::absolute(fromUtf8(*db));
    return defaultDataDir() / "library.db";
}

void setupConsole()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
}

void disableCrashDialogs()
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

} // namespace asma::cli
