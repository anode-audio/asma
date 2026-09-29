// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Db.h"

#include "asma/core/Fs.h"
#include "asma/core/Schema.h"

#include <sqlite3.h>
#include <utility>

namespace asma {

namespace {

[[noreturn]] void fail(sqlite3* db, std::string_view what)
{
    throw DbError(std::string(what) + ": " + (db ? sqlite3_errmsg(db) : "out of memory"));
}

} // namespace

Statement::Statement(sqlite3* db, std::string_view sql) : db_(db)
{
    if (sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK)
        fail(db, "prepare failed for: " + std::string(sql));
}

Statement::~Statement() { sqlite3_finalize(stmt_); }

Statement::Statement(Statement&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)), stmt_(std::exchange(other.stmt_, nullptr))
{
}

Statement& Statement::operator=(Statement&& other) noexcept
{
    if (this != &other) {
        sqlite3_finalize(stmt_);
        db_ = std::exchange(other.db_, nullptr);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

void Statement::check(int rc, std::string_view what)
{
    if (rc != SQLITE_OK) fail(db_, what);
}

Statement& Statement::bind(int index, std::int64_t value)
{
    check(sqlite3_bind_int64(stmt_, index, value), "bind failed");
    return *this;
}

Statement& Statement::bind(int index, double value)
{
    check(sqlite3_bind_double(stmt_, index, value), "bind failed");
    return *this;
}

Statement& Statement::bind(int index, std::string_view value)
{
    // A default-constructed string_view has a null data pointer, which SQLite
    // would bind as NULL. Bind an empty string instead.
    const char* data = value.data() ? value.data() : "";
    check(sqlite3_bind_text(stmt_, index, data, static_cast<int>(value.size()), SQLITE_TRANSIENT),
          "bind failed");
    return *this;
}

Statement& Statement::bindBlob(int index, const void* data, std::size_t size)
{
    check(sqlite3_bind_blob64(stmt_, index, data, static_cast<sqlite3_uint64>(size), SQLITE_TRANSIENT), "bind failed");
    return *this;
}

Statement& Statement::bindNull(int index)
{
    check(sqlite3_bind_null(stmt_, index), "bind failed");
    return *this;
}

bool Statement::step()
{
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    fail(db_, "step failed");
}

void Statement::run()
{
    while (step()) {
    }
}

void Statement::reset()
{
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

bool Statement::isNull(int column) const { return sqlite3_column_type(stmt_, column) == SQLITE_NULL; }

std::int64_t Statement::getInt(int column) const { return sqlite3_column_int64(stmt_, column); }

double Statement::getDouble(int column) const { return sqlite3_column_double(stmt_, column); }

std::string Statement::getText(int column) const
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, column));
    if (!text) return {};
    return std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column)));
}

std::vector<unsigned char> Statement::getBlob(int column) const
{
    const auto* data = static_cast<const unsigned char*>(sqlite3_column_blob(stmt_, column));
    const auto size = static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column));
    return data ? std::vector<unsigned char>(data, data + size) : std::vector<unsigned char>();
}

Db Db::openHandle(const std::string& utf8Name, int flags)
{
    sqlite3* raw = nullptr;
    const int rc = sqlite3_open_v2(utf8Name.c_str(), &raw, flags, nullptr);
    Db db(raw); // owns the handle even on failure, so it gets closed
    if (rc != SQLITE_OK) fail(raw, "cannot open database " + utf8Name);
    sqlite3_busy_timeout(raw, 5000);
    db.exec("PRAGMA foreign_keys = ON");
    return db;
}

Db Db::open(const std::filesystem::path& file)
{
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path());
    Db db = openHandle(toUtf8(file), SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    db.exec("PRAGMA journal_mode = WAL");
    db.exec("PRAGMA synchronous = NORMAL");
    migrate(db);
    return db;
}

Db Db::openReadOnly(const std::filesystem::path& file)
{
    Db db = openHandle(toUtf8(file), SQLITE_OPEN_READONLY);
    const int version = db.schemaVersion();
    if (version != currentSchemaVersion()) throw SchemaMismatchError(version);
    return db;
}

Db Db::openInMemory(int schemaVersion)
{
    Db db = openHandle(":memory:", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    migrate(db, schemaVersion < 0 ? currentSchemaVersion() : schemaVersion);
    return db;
}

Db::~Db() { sqlite3_close_v2(db_); }

Db::Db(Db&& other) noexcept : db_(std::exchange(other.db_, nullptr)) {}

Db& Db::operator=(Db&& other) noexcept
{
    if (this != &other) {
        sqlite3_close_v2(db_);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

void Db::exec(std::string_view sql)
{
    const std::string text(sql);
    char* error = nullptr;
    if (sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : "unknown error";
        sqlite3_free(error);
        throw DbError("exec failed: " + message);
    }
}

Statement Db::prepare(std::string_view sql) { return Statement(db_, sql); }

std::int64_t Db::lastInsertId() const { return sqlite3_last_insert_rowid(db_); }

SchemaMismatchError::SchemaMismatchError(int found)
    : DbError("library schema version " + std::to_string(found) + ", this asma build uses "
              + std::to_string(currentSchemaVersion())),
      found_(found)
{
}

int Db::schemaVersion()
{
    auto q = prepare("PRAGMA user_version");
    q.step();
    return static_cast<int>(q.getInt(0));
}

Transaction::Transaction(Db& db) : db_(db) { db_.exec("BEGIN IMMEDIATE"); }

Transaction::~Transaction()
{
    if (finished_) return;
    try {
        db_.exec("ROLLBACK");
    } catch (...) {
        // Nothing useful to do in a destructor.
    }
}

void Transaction::commit()
{
    db_.exec("COMMIT");
    finished_ = true;
}

} // namespace asma
