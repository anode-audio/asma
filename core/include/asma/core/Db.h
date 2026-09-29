// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace asma {

class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A library written for a different schema than this build's. Older: a
// writer (the app or the asma CLI) must open it once to migrate it. Newer:
// this build is out of date.
class SchemaMismatchError : public DbError {
public:
    explicit SchemaMismatchError(int found);
    int found() const { return found_; }

private:
    int found_;
};

// A prepared statement. Bind indices are 1-based, column indices 0-based.
class Statement {
public:
    Statement(sqlite3* db, std::string_view sql);
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, int value) { return bind(index, static_cast<std::int64_t>(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bindNull(int index);
    Statement& bindBlob(int index, const void* data, std::size_t size);

    template <typename T>
    Statement& bindOptional(int index, const std::optional<T>& value)
    {
        return value ? bind(index, *value) : bindNull(index);
    }

    bool step();  // true while a row is available
    void run();   // steps to completion, for writes
    void reset(); // resets and clears bindings for reuse

    bool isNull(int column) const;
    std::int64_t getInt(int column) const;
    double getDouble(int column) const;
    std::string getText(int column) const;
    std::vector<unsigned char> getBlob(int column) const;

private:
    void check(int rc, std::string_view what);

    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

class Db {
public:
    // Opens or creates the file (and its parent directories), enables WAL and
    // migrates to the current schema.
    static Db open(const std::filesystem::path& file);
    // Opens an existing library for reading only, as the plugin does inside a
    // host: nothing is created or migrated, and any write throws DbError.
    // Throws DbError when the file cannot be opened, SchemaMismatchError when
    // its schema is not this build's.
    static Db openReadOnly(const std::filesystem::path& file);
    // Private in-memory database migrated to schemaVersion (default: the
    // current one), for tests.
    static Db openInMemory(int schemaVersion = -1);

    ~Db();
    Db(Db&& other) noexcept;
    Db& operator=(Db&& other) noexcept;
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    void exec(std::string_view sql);
    Statement prepare(std::string_view sql);
    std::int64_t lastInsertId() const;
    int schemaVersion();
    sqlite3* handle() const { return db_; }

private:
    explicit Db(sqlite3* db) : db_(db) {}
    static Db openHandle(const std::string& utf8Name, int flags);

    sqlite3* db_ = nullptr;
};

// BEGIN IMMEDIATE on construction; ROLLBACK on destruction unless committed.
class Transaction {
public:
    explicit Transaction(Db& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Db& db_;
    bool finished_ = false;
};

} // namespace asma
