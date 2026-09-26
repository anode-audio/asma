// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace asma {

class Db;

int currentSchemaVersion();

// Applies pending migrations, each in its own transaction. Throws DbError when
// the database was written by a newer asma.
void migrate(Db& db);

} // namespace asma
