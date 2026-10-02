// SPDX-License-Identifier: GPL-3.0-only
// The editor against the approved design: tests/ui/reference/main.png is the
// design's main artboard filled with this file's demo library, the 3c2b
// areas blanked (tests/ui/reference/main.html renders it; see README.md
// there). Font rendering differs between systems, so this runs on macOS
// only; the behaviour tests run everywhere.
#include "AsmaEditor.h"
#include "PluginTestUtil.h"
#include "Signals.h"
#include "asma/core/Db.h"
#include "asma/core/Fs.h"
#include "asma/core/Library.h"
#include "asma/core/Scanner.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>

using namespace asma;
namespace fs = std::filesystem;

namespace {

constexpr int kWidth = 1280, kHeight = 800;
// A pixel differs when a channel is off by more than this. Text is drawn by
// Chrome in the reference and by JUCE here, so glyph edges never match
// exactly: each area of the design differs by 1.5% to 2.7% when the editor is
// right. Areas are judged apart, at the design's own coordinates, so a
// regression in one is not lost in the dark ground of the rest: the preview
// panel 20 px short differs by 4.5% there.
constexpr int kTolerance = 48;
constexpr double kMaxMismatch = 0.035;

struct Area {
    const char* name;
    juce::Rectangle<int> bounds;
};
const Area kAreas[] = {
    {"top bar", {0, 0, kWidth, 56}},
    {"table", {220, 56, kWidth - 220, 482}},
    {"preview", {0, 538, kWidth - 284, 236}},
    {"footer", {0, 774, kWidth, 26}},
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

struct Demo {
    test::TempDir dir;
    std::string dataDir = (dir.path() / "data").string();
    test::ScopedEnv env{"ASMA_DATA_DIR", dataDir.c_str()};
    fs::path lib = dir.path() / "Samples";
    Demo()
    {
        constexpr int rate = 8000; // small files; the waveform is not compared
        for (const auto& f : kDemo) test::writeWavFloat(lib / "Loops" / f.name, rate, {test::sine(55.0, f.seconds, 0.6, rate)});
        Db db = Db::open(dir.path() / "data" / "library.db");
        Library library(db);
        scanRoot(db, library.addRoot(lib));
    }
};

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
    const juce::Rectangle<int> masked[] = {wave.reduced(2, 14), {kWidth - 18, kHeight - 18, 18, 18}};
    juce::Image diff(juce::Image::ARGB, kWidth, kHeight, true, juce::SoftwareImageType{});
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) diff.setPixelAt(x, y, reference.getPixelAt(x, y).withMultipliedAlpha(0.25f));
    for (const Area& area : kAreas) {
        std::int64_t compared = 0, mismatched = 0;
        for (int y = area.bounds.getY(); y < area.bounds.getBottom(); ++y)
            for (int x = area.bounds.getX(); x < area.bounds.getRight(); ++x) {
                bool skip = false;
                for (const auto& m : masked) skip |= m.contains(x, y);
                if (skip) continue;
                const auto a = current.getPixelAt(x, y), b = reference.getPixelAt(x, y);
                const int d = std::max({std::abs(a.getRed() - b.getRed()), std::abs(a.getGreen() - b.getGreen()),
                                        std::abs(a.getBlue() - b.getBlue())});
                ++compared;
                if (d > kTolerance) {
                    ++mismatched;
                    diff.setPixelAt(x, y, juce::Colours::magenta);
                }
            }
        const double mismatch = static_cast<double>(mismatched) / static_cast<double>(compared);
        INFO(area.name << ": " << mismatch * 100.0 << "% of pixels differ; see " << outDir().string() << "/diff.png");
        CHECK(mismatch <= kMaxMismatch);
    }
    writePng(diff, outDir() / "diff.png");
    p.editorBeingDeleted(editor.get());
}
