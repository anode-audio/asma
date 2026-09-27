// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Query.h"

#include <algorithm>
#include <cctype>
#include <type_traits>

namespace asma {

namespace {

bool isWordByte(char c)
{
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || u >= 0x80;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

constexpr std::string_view kRowSelect =
    "SELECT f.id, r.path, f.rel_path, f.name, f.format, f.duration, ft.bpm, ft.key, ft.is_loop "
    "FROM files f JOIN roots r ON r.id = f.root_id "
    "LEFT JOIN features ft ON ft.file_id = f.id "
    "WHERE f.status = 'ok' AND r.enabled = 1";

SearchRow readRow(const Statement& s)
{
    SearchRow r;
    r.id = s.getInt(0);
    r.rootPath = s.getText(1);
    r.relPath = s.getText(2);
    r.name = s.getText(3);
    r.format = s.getText(4);
    r.duration = s.getDouble(5);
    if (!s.isNull(6)) r.bpm = s.getDouble(6);
    if (!s.isNull(7)) r.key = s.getText(7);
    if (!s.isNull(8)) r.isLoop = s.getInt(8) != 0;
    return r;
}

std::string placeholders(std::size_t count)
{
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += i ? ", ?" : "?";
    return out;
}

} // namespace

std::string ftsMatchExpression(std::string_view text)
{
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && !isWordByte(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && isWordByte(text[i])) ++i;
        if (i > start) {
            if (!out.empty()) out += ' ';
            out += '"';
            out.append(text.substr(start, i - start));
            out += "\"*";
        }
    }
    return out;
}

SqlQuery buildSearchSql(const SearchModel& m)
{
    SqlQuery q;
    q.sql = std::string(kRowSelect);

    if (const std::string match = ftsMatchExpression(m.text); !match.empty()) {
        q.sql += " AND f.id IN (SELECT rowid FROM fts_files WHERE fts_files MATCH ?)";
        q.params.emplace_back(match);
    }
    if (m.type == SampleType::Loop) q.sql += " AND ft.is_loop = 1";
    if (m.type == SampleType::OneShot) q.sql += " AND ft.is_loop = 0";
    if (m.bpmMin) {
        q.sql += " AND ft.bpm >= ?";
        q.params.emplace_back(*m.bpmMin);
    }
    if (m.bpmMax) {
        q.sql += " AND ft.bpm <= ?";
        q.params.emplace_back(*m.bpmMax);
    }
    if (!m.keys.empty()) {
        q.sql += " AND ft.key IN (" + placeholders(m.keys.size()) + ")";
        for (const auto& key : m.keys) q.params.emplace_back(key);
    }
    for (const auto& tag : m.tags) {
        q.sql += " AND f.id IN (SELECT x.file_id FROM file_tags x JOIN tags t ON t.id = x.tag_id WHERE t.name = ?)";
        q.params.emplace_back(lower(tag));
    }
    if (m.durationMin) {
        q.sql += " AND f.duration >= ?";
        q.params.emplace_back(*m.durationMin);
    }
    if (m.durationMax) {
        q.sql += " AND f.duration <= ?";
        q.params.emplace_back(*m.durationMax);
    }
    if (!m.formats.empty()) {
        q.sql += " AND f.format IN (" + placeholders(m.formats.size()) + ")";
        for (const auto& format : m.formats) q.params.emplace_back(lower(format));
    }
    if (m.rootId) {
        q.sql += " AND f.root_id = ?";
        q.params.emplace_back(*m.rootId);
    }

    const std::string direction = m.descending ? " DESC" : " ASC";
    std::string order;
    switch (m.sort) {
    case SortField::Name: order = "f.name COLLATE NOCASE" + direction; break;
    case SortField::Bpm: order = "ft.bpm IS NULL, ft.bpm" + direction; break;
    case SortField::Duration: order = "f.duration" + direction; break;
    case SortField::Key: order = "ft.key IS NULL, ft.key" + direction; break;
    }
    q.sql += " ORDER BY " + order + ", f.id LIMIT ? OFFSET ?";
    q.params.emplace_back(static_cast<std::int64_t>(m.limit));
    q.params.emplace_back(static_cast<std::int64_t>(m.offset));
    return q;
}

std::vector<SearchRow> search(Db& db, const SearchModel& model)
{
    const SqlQuery q = buildSearchSql(model);
    Statement s = db.prepare(q.sql);
    for (std::size_t i = 0; i < q.params.size(); ++i) {
        const int index = static_cast<int>(i) + 1;
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::string>) s.bind(index, std::string_view(value));
            else s.bind(index, value);
        }, q.params[i]);
    }

    std::vector<SearchRow> rows;
    while (s.step()) rows.push_back(readRow(s));
    return rows;
}

std::vector<SearchRow> rowsForIds(Db& db, const std::vector<std::int64_t>& ids)
{
    std::vector<SearchRow> rows;
    if (ids.empty()) return rows;
    Statement s = db.prepare(std::string(kRowSelect) + " AND f.id IN (" + placeholders(ids.size()) + ")");
    for (std::size_t i = 0; i < ids.size(); ++i) s.bind(static_cast<int>(i) + 1, ids[i]);
    std::vector<SearchRow> found;
    while (s.step()) found.push_back(readRow(s));
    for (const auto id : ids) {
        const auto it = std::find_if(found.begin(), found.end(), [&](const SearchRow& r) { return r.id == id; });
        if (it != found.end()) rows.push_back(*it);
    }
    return rows;
}

} // namespace asma
