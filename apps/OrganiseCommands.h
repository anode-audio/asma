// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"

#include "asma/core/Db.h"

#include <filesystem>

namespace asma::cli {

// User-data commands. The plugin runs these as its out-of-process writer, so
// each one is a single transaction and prints nothing on success.
int cmdRate(Args& args, Db& db);
int cmdFav(Args& args, Db& db);
int cmdTag(Args& args, Db& db);
int cmdCollection(Args& args, Db& db);
int cmdSearch(Args& args, Db& db);
// Reads failed files again and re-analyses them, under the writer lock,
// waiting while a scan holds it (saying so once on stdout).
int cmdRetry(Args& args, Db& db, const std::filesystem::path& dbPath);

} // namespace asma::cli
