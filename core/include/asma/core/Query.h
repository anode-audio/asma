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
    std::vector<std::string> tags; // sorted
};

struct TagCount {
    std::string name;
    std::int64_t count = 0;
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
// Every file the model matches, whatever its limit and offset.
std::int64_t countSearch(Db& db, const SearchModel& model);
// Where the file falls in the model's order, from 0, whatever its limit and
// offset; nothing when the model does not match it.
std::optional<std::int64_t> searchPosition(Db& db, const SearchModel& model, std::int64_t fileId);
// The tags of files a search can show, most used first, then by name.
std::vector<TagCount> tagCounts(Db& db);

// The model as one JSON object, for saved searches and plugin state. Paging
// (limit, offset) is view state and is left out; so are default values.
std::string searchModelToJson(const SearchModel& model);

// Reads what searchModelToJson wrote, possibly by another asma version:
// unknown fields are ignored, and so are fields with the wrong type or an
// out-of-range value. Nothing when the text is not a JSON object.
std::optional<SearchModel> searchModelFromJson(std::string_view json);

// Rows for these file ids, in the given order. Ids that are unknown, not ok
// or in a disabled root are left out.
std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids);

} // namespace asma
