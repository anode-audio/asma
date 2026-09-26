// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Schema.h"

#include "asma/core/Db.h"

#include <array>
#include <string>
#include <string_view>

namespace asma {

namespace {

// Append-only. Never edit a migration that has shipped; add a new one.
constexpr std::array<std::string_view, 1> kMigrations = {
    R"SQL(
CREATE TABLE roots (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    enabled INTEGER NOT NULL DEFAULT 1
);

CREATE TABLE files (
    id INTEGER PRIMARY KEY,
    root_id INTEGER NOT NULL REFERENCES roots(id) ON DELETE CASCADE,
    rel_path TEXT NOT NULL,
    name TEXT NOT NULL,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    content_hash TEXT,
    format TEXT NOT NULL,
    sample_rate INTEGER,
    channels INTEGER,
    bit_depth INTEGER,
    duration REAL,
    status TEXT NOT NULL CHECK (status IN ('ok', 'missing', 'failed')),
    failure_reason TEXT,
    analysis_version INTEGER NOT NULL DEFAULT 0,
    UNIQUE (root_id, rel_path)
);
CREATE INDEX files_content_hash ON files(content_hash);
CREATE INDEX files_status ON files(status);

CREATE TABLE features (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    bpm REAL,
    bpm_confidence REAL,
    key TEXT,
    key_confidence REAL,
    is_loop INTEGER,
    root_note INTEGER
);

CREATE TABLE tags (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE file_tags (
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    source TEXT NOT NULL CHECK (source IN ('auto', 'embedded', 'user')),
    PRIMARY KEY (file_id, tag_id)
);

CREATE VIRTUAL TABLE fts_files USING fts5(
    name, folder, tags,
    tokenize = 'unicode61 remove_diacritics 2',
    prefix = '2 3'
);
)SQL",
};

} // namespace

int currentSchemaVersion() { return static_cast<int>(kMigrations.size()); }

void migrate(Db& db)
{
    for (;;) {
        // Read the version inside the write transaction so two processes that
        // open the same new database cannot both apply the same migration.
        Transaction tx(db);
        const int version = db.schemaVersion();
        if (version > currentSchemaVersion())
            throw DbError("database schema version " + std::to_string(version)
                          + " is newer than this asma build supports ("
                          + std::to_string(currentSchemaVersion()) + ")");
        if (version == currentSchemaVersion()) {
            tx.commit();
            return;
        }
        db.exec(kMigrations[static_cast<std::size_t>(version)]);
        db.exec("PRAGMA user_version = " + std::to_string(version + 1));
        tx.commit();
    }
}

} // namespace asma
