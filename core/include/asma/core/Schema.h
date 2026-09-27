// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace asma {

class Db;

int currentSchemaVersion();

// Applies pending migrations up to targetVersion (default: current), each in
// its own transaction. Throws DbError when the database was written by a newer
// asma.
void migrate(Db& db, int targetVersion = -1);

} // namespace asma
