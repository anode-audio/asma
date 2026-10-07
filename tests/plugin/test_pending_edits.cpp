// SPDX-License-Identifier: GPL-3.0-only
#include "PendingEdits.h"

#include <catch2/catch_test_macros.hpp>

using asma::SearchRow;
using asma::app::PendingEdits;

namespace {

SearchRow row(std::int64_t id)
{
    SearchRow r;
    r.id = id;
    r.rating = 2;
    r.tags = {"bass"};
    return r;
}

} // namespace

TEST_CASE("a pending edit shows until the library has it", "[pending]")
{
    PendingEdits pending;
    const auto rated = pending.setRating(1, 4);
    const auto fav = pending.setFavourite(1, true);
    CHECK(pending.apply(row(1)).rating == 4);
    CHECK(pending.apply(row(1)).favourite);
    CHECK(pending.apply(row(2)).rating == 2); // other rows as they are

    pending.libraryChanged(); // not written yet: a change from elsewhere
    CHECK(pending.apply(row(1)).rating == 4);
    pending.finished(rated, true);
    pending.finished(fav, true);
    CHECK(pending.apply(row(1)).rating == 4); // written, the rows not yet refetched
    pending.libraryChanged();
    CHECK(pending.empty());
}

TEST_CASE("a failed write rolls its edit back at once", "[pending]")
{
    PendingEdits pending;
    const auto ticket = pending.setRating(1, 5);
    pending.finished(ticket, false);
    CHECK(pending.apply(row(1)).rating == 2);
    CHECK(pending.empty());
}

TEST_CASE("the last edit wins, and an earlier one failing leaves it", "[pending]")
{
    PendingEdits pending;
    const auto first = pending.setRating(1, 5);
    const auto second = pending.setRating(1, 0);
    CHECK_FALSE(pending.apply(row(1)).rating);
    pending.finished(first, false);
    CHECK_FALSE(pending.apply(row(1)).rating);
    pending.finished(second, true);
    pending.libraryChanged();
    CHECK(pending.empty());
}

TEST_CASE("pending tags replace the row's, sorted", "[pending]")
{
    PendingEdits pending;
    pending.setTags(1, {"warm", "bass"});
    CHECK(pending.apply(row(1)).tags == std::vector<std::string>{"bass", "warm"});
}
