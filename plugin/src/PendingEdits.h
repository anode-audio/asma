// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "asma/core/Query.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace asma::app {

// What the table shows before the library has it: a rating, a favourite or a
// sample's tags, changed by the user and on its way to the library. Each edit
// has a ticket. When its write succeeds the edit waits for the library's
// next change notice, which brings the stored value; when the write fails it
// goes at once, so the row shows what the library holds. JUCE-free.
class PendingEdits {
public:
    std::uint64_t setRating(std::int64_t fileId, int rating);
    std::uint64_t setFavourite(std::int64_t fileId, bool favourite);
    std::uint64_t setTags(std::int64_t fileId, std::vector<std::string> tags);

    // The write behind a ticket ended.
    void finished(std::uint64_t ticket, bool ok);
    // The library changed: written edits are in the rows now.
    void libraryChanged();

    // The row as it will be once every pending edit is in.
    SearchRow apply(SearchRow row) const;
    bool empty() const { return edits_.empty(); }

private:
    enum class Field { Rating, Favourite, Tags };
    struct Edit {
        std::uint64_t ticket = 0;
        std::int64_t fileId = 0;
        Field field = Field::Rating;
        int rating = 0;
        bool favourite = false;
        std::vector<std::string> tags;
        bool written = false;
    };
    std::uint64_t add(Edit edit);

    std::vector<Edit> edits_; // oldest first
    std::uint64_t next_ = 1;
};

} // namespace asma::app
