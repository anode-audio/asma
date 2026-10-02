// SPDX-License-Identifier: GPL-3.0-only
#include "ui/Theme.h"
#include "ui/WaveformView.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace asma;
using app::WaveformView;

namespace {

// 10 s of a full-scale square wave at 1 kHz sample rate, one channel.
std::shared_ptr<const audio::Overview> tenSeconds()
{
    audio::AudioBuffer b;
    b.sampleRate = 1000;
    b.channels = {std::vector<float>(10000)};
    for (std::size_t i = 0; i < b.channels[0].size(); ++i) b.channels[0][i] = (i / 5) % 2 ? 0.9f : -0.9f;
    return std::make_shared<const audio::Overview>(audio::makeOverview(b));
}

struct Rig {
    const juce::ScopedJuceInitialiser_GUI gui;
    WaveformView view;
    double start = -1.0, end = -2.0;
    int changes = 0, plays = 0;
    Rig()
    {
        view.setBounds(0, 0, 1000, 104); // 100 px a second
        view.setOverview(tenSeconds());
        view.onTrimChanged = [this](double s, double e) {
            start = s;
            end = e;
            ++changes;
        };
        view.onPlay = [this] { ++plays; };
    }
    juce::Colour at(int x, int y)
    {
        juce::Image image(juce::Image::ARGB, 1000, 104, true, juce::SoftwareImageType{});
        juce::Graphics g(image);
        view.paintEntireComponent(g, false);
        return image.getPixelAt(x, y);
    }
};

} // namespace

TEST_CASE("the waveform maps seconds across its width", "[waveform]")
{
    Rig rig;
    CHECK(rig.view.secondsAt(0.0f) == 0.0);
    CHECK(rig.view.secondsAt(250.0f) == Catch::Approx(2.5));
    CHECK(rig.view.secondsAt(5000.0f) == Catch::Approx(10.0)); // clamped
    CHECK(rig.view.xFor(7.5) == Catch::Approx(750.0f));
}

TEST_CASE("dragging a handle trims, once, when the mouse lets go", "[waveform]")
{
    Rig rig;
    rig.view.press(3.0f); // within reach of the start handle at 0
    rig.view.drag(150.0f);
    rig.view.drag(200.0f);
    CHECK(rig.changes == 0); // nothing restarts while the hand moves
    rig.view.release();
    CHECK(rig.changes == 1);
    CHECK(rig.start == Catch::Approx(2.0));
    CHECK(rig.end == -1.0); // the end was never touched
    CHECK(rig.plays == 0);

    rig.view.press(998.0f); // the end handle
    rig.view.drag(680.0f);
    rig.view.release();
    CHECK(rig.start == Catch::Approx(2.0));
    CHECK(rig.end == Catch::Approx(6.8));
}

TEST_CASE("the handles keep 10 ms apart and the end handle at the end means untrimmed", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 6.0);
    rig.view.press(200.0f);
    rig.view.drag(900.0f); // past the end handle
    rig.view.release();
    CHECK(rig.start == Catch::Approx(6.0 - WaveformView::kMinGap));

    rig.view.setTrim(2.0, 6.0);
    rig.view.press(600.0f);
    rig.view.drag(1200.0f); // off the right edge
    rig.view.release();
    CHECK(rig.end == -1.0);
}

TEST_CASE("double-clicking a handle puts it back; clicking elsewhere plays", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 6.0);
    rig.view.doubleClick(201.0f);
    CHECK(rig.start == 0.0);
    CHECK(rig.end == Catch::Approx(6.0));
    rig.view.doubleClick(599.0f);
    CHECK(rig.end == -1.0);
    CHECK(rig.changes == 2);
    rig.view.press(400.0f);
    rig.view.release();
    CHECK(rig.plays == 1);
    CHECK(rig.changes == 2);
}

TEST_CASE("nothing can be trimmed before the waveform arrives", "[waveform]")
{
    Rig rig;
    rig.view.setOverview(nullptr);
    rig.view.press(0.0f);
    rig.view.drag(300.0f);
    rig.view.release();
    CHECK(rig.changes == 0);
    CHECK(rig.plays == 1); // a click still plays
}

TEST_CASE("the trimmed-off parts are dimmed and the handles amber", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 6.0);
    rig.view.setPlayhead(4.0);
    using namespace app::theme;
    CHECK(rig.at(300, 52) == cyan);     // inside the trim, before the playhead
    CHECK(rig.at(500, 52) == cyanDim);  // after it
    CHECK(rig.at(400, 30) == text);     // the playhead
    CHECK(rig.at(100, 52).getBrightness() < cyanFaint.getBrightness()); // outside, under the dimming
    CHECK(rig.at(200, 60) == amber);    // the start handle
    CHECK(rig.at(599, 60) == amber);    // the end handle, inside what is kept
    CHECK(rig.at(601, 60) != amber);
}

TEST_CASE("a trim past the end of a file that got shorter stops at the file's end", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 50.0); // saved when the file was longer
    CHECK(rig.at(998, 60) == app::theme::amber); // the end handle, at the edge
    rig.view.press(200.0f);
    rig.view.drag(990.0f);
    rig.view.release();
    CHECK(rig.start <= 10.0); // never beyond the file
}

TEST_CASE("a file shorter than the handles' gap keeps them in order", "[waveform]")
{
    Rig rig;
    audio::AudioBuffer b;
    b.sampleRate = 1000;
    b.channels = {std::vector<float>(5, 0.5f)}; // 5 ms
    rig.view.setOverview(std::make_shared<const audio::Overview>(audio::makeOverview(b)));
    rig.view.press(0.0f);
    rig.view.drag(1000.0f);
    rig.view.release();
    CHECK(rig.view.trimStart() == 0.0); // nothing to trim: it stays whole
    CHECK(rig.view.trimEnd() == -1.0);
    CHECK(rig.changes == 0);            // and nothing restarts
    rig.view.press(999.0f);
    rig.view.drag(0.0f);
    rig.view.release();
    CHECK(rig.view.trimStart() <= std::max(rig.view.trimEnd(), 0.0));
}

TEST_CASE("the start handle never closes the trim on a file that got shorter", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 50.0); // the end saved past what the file now has
    rig.view.press(200.0f);
    rig.view.drag(1000.0f); // all the way right
    rig.view.release();
    CHECK(rig.start <= 10.0 - WaveformView::kMinGap); // something is always kept
}

TEST_CASE("a click on a handle that does not move it changes nothing", "[waveform]")
{
    Rig rig;
    rig.view.setTrim(2.0, 6.0);
    rig.view.press(200.0f);
    rig.view.release();
    rig.view.press(599.0f);
    rig.view.release();
    CHECK(rig.changes == 0); // nothing restarts
    rig.view.setTrim(0.0, -1.0);
    rig.view.doubleClick(1.0f); // already at the edge
    CHECK(rig.changes == 0);
}
