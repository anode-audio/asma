// SPDX-License-Identifier: GPL-3.0-only
#include "Browser.h"

namespace asma::app {

void Browser::setSearch(SearchModel model)
{
    model_ = std::move(model);
    rows_ = library_.search(model_);
}

bool Browser::poll()
{
    library_.refresh();
    if (!library_.changed()) return false;
    rows_ = library_.search(model_);
    return true;
}

std::filesystem::path Browser::path(int row) const
{
    return valid(row) ? LibraryView::pathOf(rows_[static_cast<std::size_t>(row)]) : std::filesystem::path();
}

audio::SampleInfo Browser::info(int row)
{
    return valid(row) ? library_.info(rows_[static_cast<std::size_t>(row)].id) : audio::SampleInfo{};
}

std::string Browser::contentHash(int row)
{
    return valid(row) ? library_.contentHash(rows_[static_cast<std::size_t>(row)].id) : std::string();
}

int Browser::rowOf(const std::filesystem::path& file) const
{
    // Roots are stored canonical, so compare against the canonical form.
    std::error_code ec;
    const auto wanted = std::filesystem::weakly_canonical(file, ec);
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (LibraryView::pathOf(rows_[i]) == (ec ? file : wanted)) return static_cast<int>(i);
    return -1;
}

} // namespace asma::app
