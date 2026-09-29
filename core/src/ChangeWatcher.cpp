// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ChangeWatcher.h"

namespace asma {

ChangeWatcher::ChangeWatcher(Db& db) : db_(db), last_(dataVersion()) {}

std::int64_t ChangeWatcher::dataVersion()
{
    auto q = db_.prepare("PRAGMA data_version");
    q.step();
    return q.getInt(0);
}

bool ChangeWatcher::changed()
{
    const std::int64_t now = dataVersion();
    if (now == last_) return false;
    last_ = now;
    return true;
}

} // namespace asma
