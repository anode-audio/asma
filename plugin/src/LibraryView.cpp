// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryView.h"

#include "asma/audio/Sync.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Schema.h"

namespace asma::app {

LibraryView::LibraryView(std::filesystem::path dbPath) : path_(std::move(dbPath)) {}

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

bool LibraryView::changed()
{
    if (!watcher_) return false;
    const bool fromOthers = watcher_->changed();
    const bool result = opened_ || fromOthers;
    opened_ = false;
    return result;
}

std::vector<SearchRow> LibraryView::search(const SearchModel& model)
{
    if (!db_) return {};
    return asma::search(*db_, model);
}

audio::SampleInfo LibraryView::info(std::int64_t fileId)
{
    if (!db_) return {};
    Library library(*db_);
    return audio::sampleInfo(library, fileId);
}

std::string LibraryView::contentHash(std::int64_t fileId)
{
    if (!db_) return {};
    const auto file = Library(*db_).fileById(fileId);
    return file ? file->contentHash : std::string();
}

audio::SampleInfo LibraryView::infoFor(const std::filesystem::path& file)
{
    if (!db_) return {};
    Library library(*db_);
    const auto record = library.fileByAbsolutePath(file);
    return record ? audio::sampleInfo(library, record->id) : audio::SampleInfo{};
}

std::filesystem::path LibraryView::pathOf(const SearchRow& row)
{
    return fromUtf8(row.rootPath) / fromUtf8(row.relPath);
}

} // namespace asma::app
