// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryView.h"

#include "asma/audio/Sync.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Schema.h"

namespace asma::app {

LibraryView::LibraryView(std::filesystem::path dbPath, Access access) : path_(std::move(dbPath)), access_(access) {}

LibraryView::~LibraryView() = default;

LibraryState LibraryView::refresh()
{
    if (state_ == LibraryState::Open) return state_;
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) return state_ = LibraryState::Missing;
    try {
        db_.emplace(Db::openReadOnly(path_));
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
    } catch (const DbError&) {
        state_ = LibraryState::Unreadable;
    }
    watcher_.reset();
    db_.reset();
    return state_;
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
    }
    return {};
}

template <typename Query, typename Result>
Result LibraryView::guarded(Query&& query, Result fallback)
{
    if (!db_) return fallback;
    try {
        return query();
    } catch (const std::exception&) {
        watcher_.reset();
        db_.reset();
        state_ = LibraryState::Unreadable;
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
