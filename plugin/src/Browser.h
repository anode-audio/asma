// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LibraryView.h"

#include <filesystem>
#include <string>
#include <vector>

namespace asma::app {

// The search the UI shows and its results, re-run when the library changes.
// Message thread only.
class Browser {
public:
    explicit Browser(LibraryView& library) : library_(library) {}

    void setSearch(SearchModel model);
    const SearchModel& searchModel() const { return model_; }
    const std::vector<SearchRow>& rows() const { return rows_; }
    // Samples in the library, whatever the search: "48 of 585".
    std::int64_t total() const { return total_; }
    // Samples the search matches; rows() may hold only the first page of them.
    std::int64_t matches() const { return matches_; }

    // Opens the library if it has appeared and re-runs the search when it
    // changed. Returns whether the rows may have changed.
    bool poll();

    // Row accessors; out of range gives empty values.
    std::filesystem::path path(int row) const;
    audio::SampleInfo info(int row);
    std::string contentHash(int row);
    // The row showing this file, or -1.
    int rowOf(const std::filesystem::path& file) const;

    LibraryView& library() { return library_; }

private:
    bool valid(int row) const { return row >= 0 && row < static_cast<int>(rows_.size()); }

    LibraryView& library_;
    SearchModel model_;
    std::vector<SearchRow> rows_;
    std::int64_t total_ = 0;
    std::int64_t matches_ = 0;
};

} // namespace asma::app
