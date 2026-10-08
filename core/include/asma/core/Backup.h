// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace asma {

// The library's user data as one JSON document, so a rebuilt library can take
// it back: the folders; each organised sample (rated, favourite or tagged by
// the user) by content hash and size, with its rating, favourite and user
// tags; the collections, members by hash; the saved searches, their folder and
// collection by path and name since a new library gives new ids. `writtenAt`
// is RFC 3339 UTC ("2026-10-08T09:00:00Z").
std::string backupJson(Db& db, std::string_view writtenAt);

// Writes backupJson (stamped now) to `path`: through `path` + ".tmp" and a
// rename, so a crash never leaves half a backup, moving the file already at
// `path` to `previous` first. Throws std::runtime_error when it cannot.
void writeBackup(Db& db, const std::filesystem::path& path, const std::filesystem::path& previous);

struct RestoreStats {
    std::size_t files = 0;     // organised samples found and given their data
    std::size_t unmatched = 0; // organised samples whose content is nowhere in the library
    std::size_t collections = 0;
    std::size_t searches = 0;
};

// Gives the library's files the backup's user data, matched by content hash
// and size, so a moved or renamed file keeps its data and a file present twice
// gets it twice. Collections and saved searches are made, or added to, by
// name. Fields it does not know are ignored. One transaction. Throws JsonError
// for a document that is not an asma backup.
RestoreStats restoreBackup(Db& db, std::string_view json);

// What a backup says, without restoring it; empty when it does not say.
std::vector<std::string> backupFolders(std::string_view json);
std::string backupWrittenAt(std::string_view json);

// Now as RFC 3339 UTC, to the second.
std::string utcNow();

} // namespace asma
