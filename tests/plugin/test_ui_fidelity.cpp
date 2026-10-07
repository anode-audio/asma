// SPDX-License-Identifier: GPL-3.0-only
// The editor against the approved design: tests/ui/reference/main.png is the
// design's main artboard filled with this file's demo library
// (tests/ui/reference/main.html renders it; see README.md there), and the
// popovers and the Problems panel against their own pictures. Font rendering differs between systems, so this runs on macOS
// only; the behaviour tests run everywhere.
#include "AsmaEditor.h"
#include "PluginTestUtil.h"
#include "ui/FilterPopovers.h"
#include "ui/ProblemsView.h"
#include "ui/TagsPopover.h"
#include "ui/Theme.h"
#include "Signals.h"
#include "asma/core/Analyser.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"
#include "asma/core/UserData.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

using namespace asma;
namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 1280, kHeight = 800;
// Text is drawn by Chrome in the reference and by JUCE here, and the two
// never agree pixel by pixel: compared sharp, text drawn slightly differently
// scores the same as no text at all. So both pictures are blurred first, which
// keeps where the text and the shapes are and drops how their edges were
// drawn; a pixel then differs when a channel is off by more than kTolerance.
// Areas are judged apart, at the design's own coordinates, so a regression in
// one is not lost in the dark ground of the rest. Each limit sits between what
// the area measured when right and when its content was missing (the figures
// beside it, right / missing).
constexpr int kBlur = 3; // px, each way
constexpr int kTolerance = 20;

struct Area {
    const char* name;
    juce::Rectangle<int> bounds;
    double limit; // share of pixels that may differ
};
const Area kAreas[] = {
    {"top bar", {0, 0, kWidth, 56}, 0.035},            // 2.0%
    {"sidebar", {0, 56, 220, 482}, 0.045},             // 2.8% / 7.6%
    {"chip row", {220, 56, kWidth - 220, 44}, 0.06},   // 2.3% / 15.0%
    {"table", {220, 100, kWidth - 220, 438}, 0.06},    // 3.4% / 10.8%
    {"preview", {0, 538, kWidth - 284, 236}, 0.045},   // 3.0%
    {"similar", {kWidth - 284, 538, 284, 236}, 0.14},  // 9.1% / 22.1%: almost all text
    {"footer", {0, 774, kWidth, 26}, 0.02},            // 0.6%
};

// The design's rows, as files whose names the scan reads: tempo, key, loop.
struct DemoFile {
    const char* name;
    double seconds;
};
const DemoFile kDemo[] = {
    {"Bass_Loop_Am_118.wav", 8.14},      {"Bass_Loop_Am_120.wav", 8.00},     {"Bass_Loop_Dm_120_dusty.wav", 4.00},
    {"Bass_Loop_Gm_122.wav", 7.87},      {"Bass_Loop_F_124_sub.wav", 7.74},  {"Bass_Loop_Em_124.wav", 3.87},
    {"Bass_Loop_C_126_fingered.wav", 7.62}, {"Bass_Loop_Cm_126_wet.wav", 7.62}, {"Bass_Loop_A#m_128_wobble.wav", 7.50},
    {"Bass_Loop_Fm_128.wav", 3.75},      {"Bass_Loop_Am_130_acid.wav", 7.38}, {"Bass_Loop_A#m_130.wav", 7.38},
    {"Bass_Loop_132_rolling.wav", 7.27},
};

// The design's favourites and ratings, by file.
struct Organised {
    const char* name;
    bool favourite;
    int rating; // 0: unrated
};
const Organised kOrganised[] = {
    {"Bass_Loop_Am_118.wav", false, 3},    {"Bass_Loop_Am_120.wav", true, 4},  {"Bass_Loop_Gm_122.wav", false, 2},
    {"Bass_Loop_F_124_sub.wav", true, 5},  {"Bass_Loop_C_126_fingered.wav", false, 3},
    {"Bass_Loop_A#m_128_wobble.wav", false, 1}, {"Bass_Loop_Am_130_acid.wav", false, 4},
};

struct Demo {
    test::TempDir dir;
    std::string dataDir = (dir.path() / "data").string();
    test::ScopedEnv env{"ASMA_DATA_DIR", dataDir.c_str()};
    fs::path lib = dir.path() / "Samples";
    fs::path splice = dir.path() / "Splice";
    Demo()
    {
        constexpr int rate = 8000; // small files; the waveform is not compared
        for (const auto& f : kDemo) test::writeWavFloat(lib / "Loops" / f.name, rate, {test::sine(55.0, f.seconds, 0.6, rate)});
        test::writeWavFloat(splice / "Kick_Deep.wav", rate, {test::kickHit(rate)});
        test::writeWavFloat(splice / "Snare_Tight.wav", rate, {test::hatHit(rate, 5)});
        Db db = Db::open(dir.path() / "data" / "library.db");
        Library library(db);
        UserData data(db);
        const auto root = library.addRoot(lib);
        scanRoot(db, root);
        scanRoot(db, library.addRoot(splice));
        analysePending(db); // sound profiles, for Similar
        const auto low = data.createCollection("Low end");
        for (const auto& o : kOrganised) {
            const auto id = library.fileByPath(root, std::string("Loops/") + o.name)->id;
            if (o.favourite) data.setFavourite(id, true);
            if (o.rating > 0) data.setRating(id, o.rating);
            if (o.rating >= 4) data.addToCollection(low, id);
        }
        SearchModel inAm;
        inAm.type = SampleType::Loop;
        inAm.bpmMin = 120.0;
        inAm.bpmMax = 130.0;
        inAm.keys = {"Am"};
        data.saveSearch("Loops 120\u2013130 in Am", inAm);
        SearchModel kicks;
        kicks.text = "kick";
        kicks.durationMax = 1.0;
        data.saveSearch("Short kicks", kicks);
    }
};

// A box blur, kBlur px each way, as RGB floats: where things are, not how
// their edges were drawn.
std::vector<float> blurred(const juce::Image& image)
{
    const int w = image.getWidth(), h = image.getHeight();
    std::vector<float> a(static_cast<std::size_t>(w * h * 3)), b(a.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const auto c = image.getPixelAt(x, y);
            const auto i = static_cast<std::size_t>((y * w + x) * 3);
            a[i] = c.getRed();
            a[i + 1] = c.getGreen();
            a[i + 2] = c.getBlue();
        }
    const auto pass = [&](const std::vector<float>& in, std::vector<float>& out, bool across) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int ch = 0; ch < 3; ++ch) {
                    float sum = 0.0f;
                    int n = 0;
                    for (int k = -kBlur; k <= kBlur; ++k) {
                        const int xx = across ? x + k : x, yy = across ? y : y + k;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        sum += in[static_cast<std::size_t>((yy * w + xx) * 3 + ch)];
                        ++n;
                    }
                    out[static_cast<std::size_t>((y * w + x) * 3 + ch)] = sum / static_cast<float>(n);
                }
    };
    pass(a, b, true);
    pass(b, a, false);
    return a;
}

// The share of the area's pixels (less `masked`) that differ, blurred, and
// marks them in `diff` when given.
double mismatch(const std::vector<float>& current, const std::vector<float>& reference, int width,
                juce::Rectangle<int> area, const std::vector<juce::Rectangle<int>>& masked, juce::Image* diff)
{
    std::int64_t compared = 0, mismatched = 0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x) {
            bool skip = false;
            for (const auto& m : masked) skip |= m.contains(x, y);
            if (skip) continue;
            ++compared;
            const auto i = static_cast<std::size_t>((y * width + x) * 3);
            float d = 0.0f;
            for (int ch = 0; ch < 3; ++ch) d = std::max(d, std::abs(current[i + static_cast<std::size_t>(ch)] - reference[i + static_cast<std::size_t>(ch)]));
            if (d > kTolerance) {
                ++mismatched;
                if (diff) diff->setPixelAt(x, y, juce::Colours::magenta);
            }
        }
    return compared ? static_cast<double>(mismatched) / static_cast<double>(compared) : 0.0;
}

juce::Image renderEditor(app::AsmaEditor& editor)
{
    juce::Image image(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType{});
    juce::Graphics g(image);
    editor.paintEntireComponent(g, false);
    return image;
}

fs::path outDir()
{
    const char* env = std::getenv("ASMA_UI_OUT");
    const fs::path dir = env ? fs::path(env) : fs::temp_directory_path() / "asma-ui";
    fs::create_directories(dir);
    return dir;
}

void writePng(const juce::Image& image, const fs::path& path)
{
    juce::File file(juce::String::fromUTF8(path.string().c_str()));
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
}

} // namespace

TEST_CASE("the editor matches the approved design", "[fidelity]")
{
#if !JUCE_MAC
    SKIP("font rendering differs off macOS; the reference was made there");
#endif
    Demo demo;
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaProcessor p(app::AsmaProcessor::Mode::Standalone);
    p.prepareToPlay(48000.0, 512);
    // The design's state, as a saved project: its sample selected (not
    // playing) and edited, the standalone at 180 BPM.
    app::PluginState state = p.pluginState();
    state.sync.hostBpm = 180.0;
    state.search.text = "bass loop";
    state.search.type = SampleType::Loop; // the design's two active chips
    state.search.bpmMin = 118.0;
    state.search.bpmMax = 132.0;
    state.selected = toUtf8(demo.lib / "Loops" / "Bass_Loop_Am_120.wav");
    state.edits.direction = audio::Direction::Reverse;
    state.edits.trimStart = 0.76;
    state.edits.trimEnd = 6.80;
    p.setPluginState(state);
    REQUIRE(test::waitForPreview(p, p.engine().selected()));
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.processBlock(buffer, midi); // the tempo reaches the transport
    std::unique_ptr<app::AsmaEditor> editor(dynamic_cast<app::AsmaEditor*>(p.createEditorAndMakeActive()));
    REQUIRE(editor);
    editor->setSize(kWidth, kHeight);
    editor->poll();
    REQUIRE(editor->table().getNumRows() == static_cast<int>(std::size(kDemo)));
    REQUIRE(editor->table().getSelectedRow() >= 0);
    REQUIRE_FALSE(p.engine().status().playing);
    REQUIRE(editor->preview().waveform().overview());

    const juce::Image current = renderEditor(*editor);
    writePng(current, outDir() / "current.png");
    const juce::File referenceFile(juce::String(ASMA_TEST_UI) + "/reference/main.png");
    REQUIRE(referenceFile.existsAsFile());
    const juce::Image reference = juce::ImageFileFormat::loadFrom(referenceFile);
    REQUIRE(reference.getWidth() == kWidth);
    REQUIRE(reference.getHeight() == kHeight);

    // Left out: the waveform's own shape (the demo's audio is not the
    // design's) and the window's resize corner (the design has none).
    const auto wave = editor->preview().waveform().getBounds() + editor->preview().getPosition();
    const std::vector<juce::Rectangle<int>> masked{wave.reduced(2, 14), {kWidth - 18, kHeight - 18, 18, 18}};
    juce::Image diff(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType{});
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) diff.setPixelAt(x, y, reference.getPixelAt(x, y).withMultipliedAlpha(0.25f));
    const auto cur = blurred(current), ref = blurred(reference);
    for (const Area& area : kAreas) {
        const double share = mismatch(cur, ref, kWidth, area.bounds, masked, &diff);
        INFO(area.name << ": " << share * 100.0 << "% of pixels differ; see " << outDir().string() << "/diff.png");
        CHECK(share <= area.limit);
    }
    writePng(diff, outDir() / "diff.png");
    p.editorBeingDeleted(editor.get());
}

TEST_CASE("the Key popover matches the approved design", "[fidelity]")
{
#if !JUCE_MAC
    SKIP("font rendering differs off macOS; the reference was made there");
#endif
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaLookAndFeel lnf;
    SearchModel picked;
    picked.keys = {"C", "Am"};
    app::KeyPopover popover(picked, [](const SearchModel&) {});
    popover.setLookAndFeel(&lnf);
    REQUIRE(popover.getWidth() == 360);
    REQUIRE(popover.getHeight() == 180);
    // The callout box draws the panel behind it.
    juce::Image current(juce::Image::ARGB, 360, 180, true, juce::SoftwareImageType{});
    {
        juce::Graphics g(current);
        g.fillAll(app::theme::panel);
        popover.paintEntireComponent(g, false);
    }
    writePng(current, outDir() / "key-popover.png");
    const juce::Image reference =
        juce::ImageFileFormat::loadFrom(juce::File(juce::String(ASMA_TEST_UI) + "/reference/key-popover.png"));
    REQUIRE(reference.getWidth() == 360);
    const double share = mismatch(blurred(current), blurred(reference), 360, {0, 0, 360, 180}, {}, nullptr);
    INFO("key popover: " << share * 100.0 << "% of pixels differ");
    CHECK(share <= 0.035); // 1.4% when right
    popover.setLookAndFeel(nullptr);
}

namespace {

// A component as it shows in the window, over `ground`, against its
// reference picture; the share of pixels that differ.
double againstReference(juce::Component& component, juce::Colour ground, const char* name)
{
    const int w = component.getWidth(), h = component.getHeight();
    juce::Image current(juce::Image::ARGB, w, h, true, juce::SoftwareImageType{});
    {
        juce::Graphics g(current);
        g.fillAll(ground);
        component.paintEntireComponent(g, false);
    }
    writePng(current, outDir() / (std::string(name) + ".png"));
    const juce::Image reference = juce::ImageFileFormat::loadFrom(
        juce::File(juce::String(ASMA_TEST_UI) + "/reference/" + name + ".png"));
    REQUIRE(reference.getWidth() == w);
    REQUIRE(reference.getHeight() == h);
    return mismatch(blurred(current), blurred(reference), w, {0, 0, w, h}, {}, nullptr);
}

} // namespace

TEST_CASE("the Tags popover matches the approved design", "[fidelity]")
{
#if !JUCE_MAC
    SKIP("font rendering differs off macOS; the reference was made there");
#endif
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaLookAndFeel lnf;
    app::TagsPopover popover("Bass_Loop_Am_120.wav", {{"dark", true}, {"live set", true}, {"bass", false}, {"synth", false}},
                             {{"bass", 214}, {"gritty", 31}, {"synth", 96}, {"groove", 9}}, {});
    popover.setLookAndFeel(&lnf);
    popover.field().setText("gr", true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20); // the suggestions follow the field
    REQUIRE(popover.suggestions() == juce::StringArray{"gritty", "groove"});
    REQUIRE(popover.getWidth() == 360);
    const double share = againstReference(popover, app::theme::panel, "tags-popover");
    INFO("tags popover: " << share * 100.0 << "% of pixels differ");
    CHECK(share <= 0.05); // 1.8% / 18.2% with no tags
    popover.setLookAndFeel(nullptr);
}

TEST_CASE("the Problems panel matches the approved design", "[fidelity]")
{
#if !JUCE_MAC
    SKIP("font rendering differs off macOS; the reference was made there");
#endif
    const juce::ScopedJuceInitialiser_GUI gui;
    app::AsmaLookAndFeel lnf;
    app::ProblemsView view;
    view.setLookAndFeel(&lnf);
    view.setBounds(0, 0, 840, 212);
    view.setProblems({{1, "/Samples", "Drums/Kicks/kick_broken_header.wav", Problem::Kind::Read, "not a valid WAV header"},
                      {2, "/Samples", "Pads/pad_long_take.flac", Problem::Kind::Read, "unexpected end of stream"},
                      {3, "/Samples", "Vocals/Chops/vox_chop_07.mp3", Problem::Kind::Analysis, "the file is silent"}});
    view.setRetrying({2}, false);
    const double share = againstReference(view, app::theme::surface, "problems");
    INFO("problems panel: " << share * 100.0 << "% of pixels differ");
    CHECK(share <= 0.05); // 2.7% / 10.3% with no files
    view.setLookAndFeel(nullptr);
}
