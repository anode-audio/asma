// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"

#include <filesystem>

namespace asma::cli {

enum ExitCode { kOk = 0, kError = 1, kUsage = 2, kLocked = 3 };

// --db when given, else defaultDataDir() / "library.db".
std::filesystem::path resolveDbPath(Args& args);

// UTF-8 console output on Windows; no-op elsewhere.
void setupConsole();

// A crash must end the process at once so a supervisor can restart it, not
// wait behind a Windows Error Reporting dialog: turns off the crash dialog
// and abort()'s report and message box. No-op elsewhere.
void disableCrashDialogs();

} // namespace asma::cli
