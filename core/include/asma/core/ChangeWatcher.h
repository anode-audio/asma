// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>

namespace asma {

// Tells a reader when another connection (the scanner, the app, the asma CLI
// run by a plugin) has committed to the library, so the UI can refresh what
// it shows. Polls SQLite's PRAGMA data_version, which is cheap enough to call
// from a UI timer. Commits made through the watched connection itself do not
// count.
class ChangeWatcher {
public:
    explicit ChangeWatcher(Db& db);

    // True when other connections have committed since the last call (or
    // since construction); several commits in between still give one true.
    bool changed();

private:
    std::int64_t dataVersion();

    Db& db_;
    std::int64_t last_;
};

} // namespace asma
