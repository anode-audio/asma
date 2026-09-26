// SPDX-License-Identifier: GPL-3.0-only
#include "CliCommon.h"

#include "asma/core/Fs.h"

#ifdef _WIN32
#include <windows.h>
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

} // namespace asma::cli
