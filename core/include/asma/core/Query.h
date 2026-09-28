// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Db.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace asma {

enum class SampleType { Any, Loop, OneShot };
enum class SortField { Name, Bpm, Duration, Key, Rating };

struct SearchModel {
    std::string text;                  // every word must prefix-match name, folder or tags
    SampleType type = SampleType::Any;
    std::optional<double> bpmMin;
    std::optional<double> bpmMax;
    std::vector<std::string> keys;     // canonical keys, any of
    std::vector<std::string> tags;     // all of
    std::optional<double> durationMin; // seconds
    std::optional<double> durationMax;
    std::vector<std::string> formats;  // any of
    std::optional<std::int64_t> rootId;
    std::optional<int> minRating;              // 1 to 5; unrated files never match
    bool favouritesOnly = false;
    std::optional<std::int64_t> collectionId;
    SortField sort = SortField::Name;
    bool descending = false;
    int limit = 500;
    int offset = 0;
};

struct SearchRow {
    std::int64_t id = 0;
    std::string rootPath;
    std::string relPath;
    std::string name;
    std::string format;
    double duration = 0.0;
    std::optional<double> bpm;
    std::optional<std::string> key;
    std::optional<bool> isLoop;
    std::optional<int> rating;
    bool favourite = false;
};

using SqlParam = std::variant<std::int64_t, double, std::string>;

struct SqlQuery {
    std::string sql;
    std::vector<SqlParam> params;
};

// Each word of the text as a quoted FTS5 prefix term. Empty when the text has
// no word characters, which means "no text filter".
std::string ftsMatchExpression(std::string_view text);

SqlQuery buildSearchSql(const SearchModel& model);
std::vector<SearchRow> search(Db& db, const SearchModel& model);

// Rows for these file ids, in the given order. Ids that are unknown, not ok
// or in a disabled root are left out.
std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids);

} // namespace asma
