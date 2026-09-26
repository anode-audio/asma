// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Library.h"

#include "asma/core/Fs.h"

#include <cctype>

namespace fs = std::filesystem;

namespace asma {

namespace {

constexpr std::string_view kFileColumns =
    "id, root_id, rel_path, size, mtime, content_hash, format, sample_rate, channels, "
    "bit_depth, duration, status, failure_reason";

std::string_view statusText(FileStatus s)
{
    switch (s) {
    case FileStatus::Ok: return "ok";
    case FileStatus::Missing: return "missing";
    case FileStatus::Failed: return "failed";
    }
    return "ok";
}

FileStatus statusFromText(std::string_view s)
{
    if (s == "missing") return FileStatus::Missing;
    if (s == "failed") return FileStatus::Failed;
    return FileStatus::Ok;
}

std::string_view sourceText(TagSource s)
{
    switch (s) {
    case TagSource::Auto: return "auto";
    case TagSource::Embedded: return "embedded";
    case TagSource::User: return "user";
    }
    return "auto";
}

TagSource sourceFromText(std::string_view s)
{
    if (s == "user") return TagSource::User;
    if (s == "embedded") return TagSource::Embedded;
    return TagSource::Auto;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string baseName(std::string_view relPath)
{
    const auto slash = relPath.rfind('/');
    return std::string(slash == std::string_view::npos ? relPath : relPath.substr(slash + 1));
}

FileRecord readFile(const Statement& s)
{
    FileRecord f;
    f.id = s.getInt(0);
    f.rootId = s.getInt(1);
    f.relPath = s.getText(2);
    f.size = s.getInt(3);
    f.mtime = s.getInt(4);
    f.contentHash = s.getText(5);
    f.format = s.getText(6);
    f.sampleRate = static_cast<int>(s.getInt(7));
    f.channels = static_cast<int>(s.getInt(8));
    f.bitDepth = static_cast<int>(s.getInt(9));
    f.duration = s.getDouble(10);
    f.status = statusFromText(s.getText(11));
    f.failureReason = s.getText(12);
    return f;
}

std::vector<FileRecord> readFiles(Statement& s)
{
    std::vector<FileRecord> out;
    while (s.step()) out.push_back(readFile(s));
    return out;
}

// Binds the 12 data columns of a file row starting at parameter `first`.
void bindFileColumns(Statement& s, int first, const FileRecord& f)
{
    s.bind(first, f.rootId);
    s.bind(first + 1, std::string_view(f.relPath));
    s.bind(first + 2, std::string_view(baseName(f.relPath)));
    s.bind(first + 3, f.size);
    s.bind(first + 4, f.mtime);
    if (f.contentHash.empty()) s.bindNull(first + 5);
    else s.bind(first + 5, std::string_view(f.contentHash));
    s.bind(first + 6, std::string_view(f.format));
    s.bind(first + 7, f.sampleRate);
    s.bind(first + 8, f.channels);
    s.bind(first + 9, f.bitDepth);
    s.bind(first + 10, f.duration);
    s.bind(first + 11, statusText(f.status));
    if (f.failureReason.empty()) s.bindNull(first + 12);
    else s.bind(first + 12, std::string_view(f.failureReason));
}

} // namespace

std::int64_t Library::addRoot(const fs::path& dir)
{
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(fs::absolute(dir), ec);
    if (ec) canonical = fs::absolute(dir);
    if (!canonical.has_filename() && canonical != canonical.root_path()) canonical = canonical.parent_path();
    const std::string path = toUtf8(canonical);

    auto select = db_.prepare("SELECT id FROM roots WHERE path = ?");
    select.bind(1, std::string_view(path));
    if (select.step()) return select.getInt(0);

    auto insert = db_.prepare("INSERT INTO roots(path) VALUES (?)");
    insert.bind(1, std::string_view(path));
    insert.run();
    return db_.lastInsertId();
}

std::vector<Root> Library::roots()
{
    std::vector<Root> out;
    auto q = db_.prepare("SELECT id, path, enabled FROM roots ORDER BY id");
    while (q.step()) out.push_back({q.getInt(0), q.getText(1), q.getInt(2) != 0});
    return out;
}

std::optional<Root> Library::root(std::int64_t id)
{
    auto q = db_.prepare("SELECT id, path, enabled FROM roots WHERE id = ?");
    q.bind(1, id);
    if (!q.step()) return std::nullopt;
    return Root{q.getInt(0), q.getText(1), q.getInt(2) != 0};
}

std::vector<FileRecord> Library::filesInRoot(std::int64_t rootId)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns) + " FROM files WHERE root_id = ?");
    q.bind(1, rootId);
    return readFiles(q);
}

std::optional<FileRecord> Library::fileById(std::int64_t id)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns) + " FROM files WHERE id = ?");
    q.bind(1, id);
    if (!q.step()) return std::nullopt;
    return readFile(q);
}

std::optional<FileRecord> Library::fileByPath(std::int64_t rootId, std::string_view relPath)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns) + " FROM files WHERE root_id = ? AND rel_path = ?");
    q.bind(1, rootId).bind(2, relPath);
    if (!q.step()) return std::nullopt;
    return readFile(q);
}

std::vector<FileRecord> Library::relinkCandidates(std::string_view contentHash, std::int64_t size)
{
    auto q = db_.prepare("SELECT " + std::string(kFileColumns)
                         + " FROM files WHERE status = 'missing' AND content_hash = ? AND size = ? ORDER BY id");
    q.bind(1, contentHash).bind(2, size);
    return readFiles(q);
}

std::int64_t Library::insertFile(const FileRecord& file)
{
    auto q = db_.prepare("INSERT INTO files(root_id, rel_path, name, size, mtime, content_hash, format, "
                         "sample_rate, channels, bit_depth, duration, status, failure_reason) "
                         "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    bindFileColumns(q, 1, file);
    q.run();
    const auto id = db_.lastInsertId();
    refreshFts(id);
    return id;
}

void Library::updateFile(const FileRecord& file)
{
    auto q = db_.prepare("UPDATE files SET root_id = ?, rel_path = ?, name = ?, size = ?, mtime = ?, "
                         "content_hash = ?, format = ?, sample_rate = ?, channels = ?, bit_depth = ?, "
                         "duration = ?, status = ?, failure_reason = ? WHERE id = ?");
    bindFileColumns(q, 1, file);
    q.bind(14, file.id);
    q.run();
    refreshFts(file.id);
}

void Library::setStatus(std::int64_t fileId, FileStatus status, std::string_view reason)
{
    auto q = db_.prepare("UPDATE files SET status = ?, failure_reason = ? WHERE id = ?");
    q.bind(1, statusText(status));
    if (reason.empty()) q.bindNull(2);
    else q.bind(2, reason);
    q.bind(3, fileId);
    q.run();
}

void Library::resetAnalysis(std::int64_t fileId)
{
    auto q = db_.prepare("UPDATE files SET analysis_version = 0 WHERE id = ?");
    q.bind(1, fileId);
    q.run();
}

void Library::setDerived(std::int64_t fileId, const DerivedInfo& info)
{
    auto features = db_.prepare(
        "INSERT INTO features(file_id, bpm, bpm_confidence, key, key_confidence, is_loop, root_note) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(file_id) DO UPDATE SET bpm = excluded.bpm, bpm_confidence = excluded.bpm_confidence, "
        "key = excluded.key, key_confidence = excluded.key_confidence, is_loop = excluded.is_loop, "
        "root_note = excluded.root_note");
    features.bind(1, fileId);
    features.bindOptional(2, info.bpm);
    if (info.bpm) features.bind(3, info.bpmConfidence);
    else features.bindNull(3);
    if (info.key) features.bind(4, std::string_view(*info.key));
    else features.bindNull(4);
    if (info.key) features.bind(5, info.keyConfidence);
    else features.bindNull(5);
    features.bindOptional(6, info.isLoop);
    features.bindOptional(7, info.rootNote);
    features.run();

    auto clear = db_.prepare("DELETE FROM file_tags WHERE file_id = ? AND source IN ('auto', 'embedded')");
    clear.bind(1, fileId);
    clear.run();

    auto insert = db_.prepare("INSERT OR IGNORE INTO file_tags(file_id, tag_id, source) VALUES (?, ?, ?)");
    for (const auto& [name, source] : info.tags) {
        insert.bind(1, fileId).bind(2, ensureTag(name)).bind(3, sourceText(source));
        insert.run();
        insert.reset();
    }
    refreshFts(fileId);
}

std::optional<DerivedInfo> Library::derived(std::int64_t fileId)
{
    auto q = db_.prepare("SELECT bpm, bpm_confidence, key, key_confidence, is_loop, root_note "
                         "FROM features WHERE file_id = ?");
    q.bind(1, fileId);
    if (!q.step()) return std::nullopt;
    DerivedInfo d;
    if (!q.isNull(0)) {
        d.bpm = q.getDouble(0);
        d.bpmConfidence = q.getDouble(1);
    }
    if (!q.isNull(2)) {
        d.key = q.getText(2);
        d.keyConfidence = q.getDouble(3);
    }
    if (!q.isNull(4)) d.isLoop = q.getInt(4) != 0;
    if (!q.isNull(5)) d.rootNote = static_cast<int>(q.getInt(5));
    return d;
}

void Library::addUserTag(std::int64_t fileId, std::string_view tag)
{
    auto q = db_.prepare("INSERT INTO file_tags(file_id, tag_id, source) VALUES (?, ?, 'user') "
                         "ON CONFLICT(file_id, tag_id) DO UPDATE SET source = 'user'");
    q.bind(1, fileId).bind(2, ensureTag(tag));
    q.run();
    refreshFts(fileId);
}

std::vector<std::pair<std::string, TagSource>> Library::tags(std::int64_t fileId)
{
    std::vector<std::pair<std::string, TagSource>> out;
    auto q = db_.prepare("SELECT t.name, ft.source FROM file_tags ft JOIN tags t ON t.id = ft.tag_id "
                         "WHERE ft.file_id = ? ORDER BY t.name");
    q.bind(1, fileId);
    while (q.step()) out.emplace_back(q.getText(0), sourceFromText(q.getText(1)));
    return out;
}

std::int64_t Library::ensureTag(std::string_view name)
{
    const std::string normalised = lower(name);
    auto insert = db_.prepare("INSERT OR IGNORE INTO tags(name) VALUES (?)");
    insert.bind(1, std::string_view(normalised));
    insert.run();
    auto select = db_.prepare("SELECT id FROM tags WHERE name = ?");
    select.bind(1, std::string_view(normalised));
    select.step();
    return select.getInt(0);
}

void Library::refreshFts(std::int64_t fileId)
{
    auto file = db_.prepare("SELECT rel_path FROM files WHERE id = ?");
    file.bind(1, fileId);
    if (!file.step()) return;
    const std::string relPath = file.getText(0);

    const auto slash = relPath.rfind('/');
    std::string folder = slash == std::string::npos ? std::string() : relPath.substr(0, slash);
    for (char& c : folder)
        if (c == '/') c = ' ';
    std::string name = baseName(relPath);
    if (const auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) name.resize(dot);

    std::string tagText;
    for (const auto& [tag, source] : tags(fileId)) {
        if (!tagText.empty()) tagText += ' ';
        tagText += tag;
    }

    auto remove = db_.prepare("DELETE FROM fts_files WHERE rowid = ?");
    remove.bind(1, fileId);
    remove.run();
    auto insert = db_.prepare("INSERT INTO fts_files(rowid, name, folder, tags) VALUES (?, ?, ?, ?)");
    insert.bind(1, fileId)
        .bind(2, std::string_view(name))
        .bind(3, std::string_view(folder))
        .bind(4, std::string_view(tagText));
    insert.run();
}

} // namespace asma
