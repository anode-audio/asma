// SPDX-License-Identifier: GPL-3.0-only
#include "LibraryFixture.h"
#include "LibraryView.h"
#include "asma/core/Analyser.h"
#include "ui/SimilarView.h"

#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::LibraryView;
using app::SimilarResult;
using app::SimilarView;
namespace fs = std::filesystem;

namespace {

void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

std::int64_t idOf(const test::LibraryFixture& f, const fs::path& file)
{
    Db db = Db::open(f.dbPath);
    return Library(db).fileByAbsolutePath(file)->id;
}

} // namespace

TEST_CASE("the library lists what sounds like a sample, nearest first", "[similar]")
{
    test::LibraryFixture f;
    f.scan();
    {
        Db db = Db::open(f.dbPath);
        analysePending(db); // sound profiles
    }
    LibraryView library(f.dbPath);
    library.refresh();
    const SimilarResult r = library.similar(idOf(f, f.kick));
    CHECK(r.state == SimilarResult::State::Ok);
    REQUIRE(r.matches.size() == 2); // everything else in a library of three
    CHECK(r.matches[0].distance <= r.matches[1].distance);
    CHECK(r.matches[0].distance >= 0.0);
    CHECK(r.matches[0].row.name != "Kick_01.wav"); // never itself
}

TEST_CASE("a sample not analysed yet has no similar list, and says so", "[similar]")
{
    test::LibraryFixture f;
    f.scan(); // scanned, not analysed
    LibraryView library(f.dbPath);
    library.refresh();
    CHECK(library.similar(idOf(f, f.kick)).state == SimilarResult::State::NotAnalysed);
}

TEST_CASE("the Similar list shows its matches, or why it has none", "[similar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SimilarView view;
    view.setBounds(0, 0, 284, 235);
    CHECK(view.message() == "Select a sample");
    SimilarResult r;
    r.state = SimilarResult::State::NotAnalysed;
    view.setResult(r);
    CHECK(view.message() == "Not analysed yet");
    r.state = SimilarResult::State::Failed;
    view.setResult(r);
    CHECK(view.message() == "No similar samples");
    r.state = SimilarResult::State::Ok;
    SearchRow a;
    a.id = 7;
    a.name = "Bass_Loop_Am_118.wav";
    r.matches = {{a, 0.08}};
    view.setResult(r);
    CHECK(view.message().isEmpty());
    REQUIRE(view.rowCount() == 1);
    CHECK(view.row(0).getButtonText() == "Bass_Loop_Am_118.wav");
    CHECK(view.distanceText(0) == "0.08");
    std::int64_t picked = 0;
    view.onPick = [&](const SearchRow& row) { picked = row.id; };
    view.row(0).triggerClick();
    settle();
    CHECK(picked == 7);
    view.clear();
    CHECK(view.message() == "Select a sample");
}

TEST_CASE("the Similar list shows only the rows that fit whole", "[similar]")
{
    const juce::ScopedJuceInitialiser_GUI gui;
    SimilarView view;
    view.setBounds(0, 0, 284, 235);
    SimilarResult r;
    r.state = SimilarResult::State::Ok;
    for (int i = 0; i < 10; ++i) {
        SearchRow row;
        row.id = i + 1;
        row.name = "Sample_" + std::to_string(i) + ".wav";
        r.matches.push_back({row, 0.1 * i});
    }
    view.setResult(r);
    for (int i = 0; i < view.rowCount(); ++i)
        if (view.row(i).isVisible()) CHECK(view.getLocalBounds().contains(view.row(i).getBounds()));
    CHECK(view.row(0).isVisible());
    CHECK_FALSE(view.row(9).isVisible()); // ten do not fit in the panel
}
