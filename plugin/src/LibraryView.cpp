// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryView.h"

#include "asma/audio/Sync.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Schema.h"
#include "asma/core/Similar.h"

namespace asma::app {

LibraryView::LibraryView(std::filesystem::path dbPath, Access access) : path_(std::move(dbPath)), access_(access) {}

LibraryView::~LibraryView() = default;

void LibraryView::suspend(bool on)
{
    if (on == suspended_) return;
    suspended_ = on;
    if (!on) return; // the next refresh opens it again
    watcher_.reset();
    db_.reset();
    state_ = LibraryState::Damaged;
}

LibraryState LibraryView::refresh()
{
    if (suspended_) return state_ = LibraryState::Damaged;
    if (state_ == LibraryState::Open) {
        // A rebuilt library replaces the file: follow it to the new one.
        const std::string now = fileIdentity(path_);
        if (now.empty() || now == identity_) return state_;
        watcher_.reset();
        db_.reset();
        state_ = LibraryState::Missing;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return state_ = LibraryState::Missing;
    try {
        identity_ = fileIdentity(path_);
        db_.emplace(Db::openReadOnly(path_));
        // Opening reads only the header: a damaged schema shows at the first
        // statement, so make one now rather than call the library open.
        db_->prepare("SELECT id FROM files LIMIT 1").step();
        watcher_ = std::make_unique<ChangeWatcher>(*db_);
        opened_ = true;
        return state_ = LibraryState::Open;
    } catch (const SchemaMismatchError& e) {
        state_ = e.found() < currentSchemaVersion() ? LibraryState::Outdated : LibraryState::TooNew;
        if (state_ == LibraryState::Outdated && access_ == Access::MayMigrate) {
            try {
                Db::open(path_); // migrates, then closes
                access_ = Access::ReadOnly; // one attempt, so a failed migration cannot loop
                const LibraryState result = refresh();
                access_ = Access::MayMigrate;
                return result;
            } catch (const std::exception&) {
                state_ = LibraryState::Unreadable;
            }
        }
    } catch (const DbError& e) {
        close(e.what());
        return state_;
    }
    watcher_.reset();
    db_.reset();
    return state_;
}

void LibraryView::close(const std::string& why)
{
    watcher_.reset();
    db_.reset();
    const bool damaged = why.find("malformed") != std::string::npos || why.find("not a database") != std::string::npos
                      || why.find("corrupt") != std::string::npos;
    state_ = damaged ? LibraryState::Damaged : LibraryState::Unreadable;
    damageSeen_ = damageSeen_ || damaged;
}

std::string LibraryView::message() const
{
    switch (state_) {
    case LibraryState::Missing: return "No library yet. Add a sample folder in the asma app, or run asma scan.";
    case LibraryState::Open: return {};
    case LibraryState::Outdated:
        return "This library was made by an older asma. Open the asma app once, or run asma scan, to update it.";
    case LibraryState::TooNew: return "This library was made by a newer asma. Update asma to read it.";
    case LibraryState::Unreadable: return "The library file cannot be read: " + toUtf8(path_);
    case LibraryState::Damaged: return "The library is damaged. asma is rebuilding it.";
    }
    return {};
}

template <typename Query, typename Result>
Result LibraryView::guarded(Query&& query, Result fallback)
{
    if (!db_) return fallback;
    try {
        return query();
    } catch (const std::exception& e) {
        close(e.what());
        return fallback;
    }
}

bool LibraryView::changed()
{
    const bool opened = opened_;
    opened_ = false;
    return guarded([&] { return watcher_->changed() || opened; }, false);
}

std::vector<SearchRow> LibraryView::search(const SearchModel& model)
{
    return guarded([&] { return asma::search(*db_, model); }, std::vector<SearchRow>{});
}

std::int64_t LibraryView::matchCount(const SearchModel& model)
{
    return guarded([&] { return asma::countSearch(*db_, model); }, std::int64_t{0});
}

std::optional<std::int64_t> LibraryView::position(const SearchModel& model, std::int64_t fileId)
{
    return guarded([&] { return asma::searchPosition(*db_, model, fileId); }, std::optional<std::int64_t>{});
}

std::optional<std::int64_t> LibraryView::fileId(const std::filesystem::path& file)
{
    return guarded(
        [&]() -> std::optional<std::int64_t> {
            const auto record = Library(*db_).fileByAbsolutePath(file);
            return record ? std::optional<std::int64_t>(record->id) : std::nullopt;
        },
        std::optional<std::int64_t>{});
}

std::vector<Root> LibraryView::roots()
{
    return guarded([&] { return Library(*db_).roots(); }, std::vector<Root>{});
}

std::vector<Collection> LibraryView::collections()
{
    return guarded([&] { return UserData(*db_).collections(); }, std::vector<Collection>{});
}

std::vector<std::int64_t> LibraryView::collectionsOf(std::int64_t fileId)
{
    return guarded(
        [&] {
            auto q = db_->prepare("SELECT collection_id FROM collection_items WHERE file_id = ? ORDER BY collection_id");
            q.bind(1, fileId);
            std::vector<std::int64_t> ids;
            while (q.step()) ids.push_back(q.getInt(0));
            return ids;
        },
        std::vector<std::int64_t>{});
}

std::vector<std::pair<std::string, TagSource>> LibraryView::tagsOf(std::int64_t fileId)
{
    return guarded([&] { return Library(*db_).tags(fileId); }, std::vector<std::pair<std::string, TagSource>>{});
}

std::vector<SavedSearch> LibraryView::savedSearches()
{
    return guarded([&] { return UserData(*db_).savedSearches(); }, std::vector<SavedSearch>{});
}

SimilarResult LibraryView::similar(std::int64_t fileId, int limit)
{
    return guarded(
        [&] {
            SimilarResult out;
            auto analysed = db_->prepare("SELECT feature_vector IS NOT NULL FROM features WHERE file_id = ?");
            analysed.bind(1, fileId);
            if (!analysed.step() || analysed.getInt(0) == 0) {
                out.state = SimilarResult::State::NotAnalysed;
                return out;
            }
            const auto found = findSimilar(*db_, fileId, limit);
            std::vector<std::int64_t> ids;
            for (const auto& m : found) ids.push_back(m.id);
            const auto rows = rowsForIds(*db_, ids); // in the order asked
            for (const auto& row : rows)
                for (const auto& m : found)
                    if (m.id == row.id) out.matches.push_back({row, 1.0 - m.similarity});
            out.state = SimilarResult::State::Ok;
            return out;
        },
        SimilarResult{});
}

std::vector<TagCount> LibraryView::tagCounts()
{
    return guarded([&] { return asma::tagCounts(*db_); }, std::vector<TagCount>{});
}

std::int64_t LibraryView::problemCount()
{
    return guarded(
        [&] {
            auto s = db_->prepare("SELECT COUNT(*) FROM files f JOIN roots r ON r.id = f.root_id WHERE r.enabled = 1 "
                                  "AND (f.status = 'failed' OR (f.status = 'ok' AND f.analysis_error IS NOT NULL))");
            return s.step() ? s.getInt(0) : std::int64_t{0};
        },
        std::int64_t{0});
}

std::vector<Problem> LibraryView::problems()
{
    return guarded([&] { return Library(*db_).problems(); }, std::vector<Problem>{});
}

std::int64_t LibraryView::sampleCount()
{
    return guarded(
        [&] {
            // The same files search() starts from, before any filter.
            auto s = db_->prepare("SELECT COUNT(*) FROM files f JOIN roots r ON r.id = f.root_id "
                                  "WHERE f.status = 'ok' AND r.enabled = 1");
            return s.step() ? s.getInt(0) : std::int64_t{0};
        },
        std::int64_t{0});
}

audio::SampleInfo LibraryView::info(std::int64_t fileId)
{
    return guarded(
        [&] {
            Library library(*db_);
            return audio::sampleInfo(library, fileId);
        },
        audio::SampleInfo{});
}

std::string LibraryView::contentHash(std::int64_t fileId)
{
    return guarded(
        [&] {
            const auto file = Library(*db_).fileById(fileId);
            return file ? file->contentHash : std::string();
        },
        std::string{});
}

audio::SampleInfo LibraryView::infoFor(const std::filesystem::path& file)
{
    return guarded(
        [&] {
            Library library(*db_);
            const auto record = library.fileByAbsolutePath(file);
            return record ? audio::sampleInfo(library, record->id) : audio::SampleInfo{};
        },
        audio::SampleInfo{});
}

std::filesystem::path LibraryView::pathOf(const SearchRow& row)
{
    return fromUtf8(row.rootPath) / fromUtf8(row.relPath);
}

} // namespace asma::app
