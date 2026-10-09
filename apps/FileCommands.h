// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"
#include "asma/core/Db.h"

#include <filesystem>

namespace asma::cli {

// File operations: rename, move, trash, remove-folder and undo, each under
// the writer lock (exit 3 when another process holds it), each first rolling
// back what an interrupted process left. Each prints the group's label, or
// with --json one line: {"group":N,"label":"...","files":[...]}.
int cmdFileOperation(const std::string& command, Args& args, Db& db, const std::filesystem::path& dbPath);
// The newest groups: "id<TAB>at<TAB>state<TAB>label", or JSON lines.
int cmdHistory(Args& args, Db& db);
// root add, refusing a folder inside one and merging those inside it with --merge.
int cmdRootAdd(Args& args, Db& db);

} // namespace asma::cli
