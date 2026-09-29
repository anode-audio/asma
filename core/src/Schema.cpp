// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Schema.h"

#include "asma/core/Db.h"

#include <array>
#include <string>
#include <string_view>

namespace asma {

namespace {

// Append-only. Never edit a migration that has shipped; add a new one.
constexpr std::array<std::string_view, 3> kMigrations = {
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
    R"SQL(
ALTER TABLE features ADD COLUMN bpm_source TEXT CHECK (bpm_source IN ('embedded', 'filename', 'analysis'));
ALTER TABLE features ADD COLUMN key_source TEXT CHECK (key_source IN ('embedded', 'filename', 'analysis'));
ALTER TABLE features ADD COLUMN loop_source TEXT CHECK (loop_source IN ('embedded', 'filename', 'analysis'));
ALTER TABLE features ADD COLUMN peak REAL;
ALTER TABLE features ADD COLUMN lufs REAL;
ALTER TABLE features ADD COLUMN centroid REAL;
ALTER TABLE features ADD COLUMN rolloff REAL;
ALTER TABLE features ADD COLUMN flatness REAL;
ALTER TABLE features ADD COLUMN onset_density REAL;
ALTER TABLE features ADD COLUMN feature_vector BLOB;
ALTER TABLE files ADD COLUMN analysis_error TEXT;

-- Version 1 only stored embedded (confidence 1.0) or file-name values.
UPDATE features SET bpm_source = CASE WHEN bpm_confidence >= 1.0 THEN 'embedded' ELSE 'filename' END
    WHERE bpm IS NOT NULL;
UPDATE features SET key_source = 'filename' WHERE key IS NOT NULL;
UPDATE features SET loop_source = 'filename' WHERE is_loop IS NOT NULL;

CREATE INDEX files_analysis_pending ON files(analysis_version) WHERE status = 'ok';
)SQL",
    R"SQL(
CREATE TABLE ratings (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE,
    rating INTEGER NOT NULL CHECK (rating BETWEEN 1 AND 5)
);

CREATE TABLE favourites (
    file_id INTEGER PRIMARY KEY REFERENCES files(id) ON DELETE CASCADE
);

-- AUTOINCREMENT: saved searches refer to collections by id, so an id must
-- never be handed to a new collection after its owner is deleted.
CREATE TABLE collections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL UNIQUE COLLATE NOCASE
);

CREATE TABLE collection_items (
    collection_id INTEGER NOT NULL REFERENCES collections(id) ON DELETE CASCADE,
    file_id INTEGER NOT NULL REFERENCES files(id) ON DELETE CASCADE,
    PRIMARY KEY (collection_id, file_id)
);
CREATE INDEX collection_items_file ON collection_items(file_id);

CREATE TABLE saved_searches (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE COLLATE NOCASE,
    model TEXT NOT NULL
);
)SQL",
};

} // namespace

int currentSchemaVersion() { return static_cast<int>(kMigrations.size()); }

void migrate(Db& db, int targetVersion)
{
    if (targetVersion < 0 || targetVersion > currentSchemaVersion()) targetVersion = currentSchemaVersion();
    for (;;) {
        // Read the version inside the write transaction so two processes that
        // open the same new database cannot both apply the same migration.
        Transaction tx(db);
        const int version = db.schemaVersion();
        if (version > currentSchemaVersion()) throw SchemaMismatchError(version);
        if (version >= targetVersion) {
            tx.commit();
            return;
        }
        db.exec(kMigrations[static_cast<std::size_t>(version)]);
        db.exec("PRAGMA user_version = " + std::to_string(version + 1));
        tx.commit();
    }
}

} // namespace asma
