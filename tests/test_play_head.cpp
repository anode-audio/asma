// SPDX-License-Identifier: GPL-3.0-only
#include "Signals.h"
#include "asma/audio/PlayHead.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

using namespace asma::audio;

namespace {

// 1, 2, 3, ... so a zero always means silence or a fade.
std::shared_ptr<AudioBuffer> counting(int frames, int rate = 1000, int channels = 1)
{
    auto b = std::make_shared<AudioBuffer>();
    b->sampleRate = rate;
    for (int c = 0; c < channels; ++c) {
        std::vector<float> ch(static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i) ch[static_cast<std::size_t>(i)] = static_cast<float>(i + 1) * (c == 0 ? 1.0f : -1.0f);
        b->channels.push_back(std::move(ch));
    }
    return b;
}

struct Rendered {
    std::vector<float> left, right;
    int sounding = 0;
};

Rendered render(PlayHead& head, int n)
{
    Rendered r;
    r.left.assign(static_cast<std::size_t>(n), 99.0f);
    r.right.assign(static_cast<std::size_t>(n), 99.0f);
    float* out[] = {r.left.data(), r.right.data()};
    r.sounding = head.render(out, n);
    return r;
}

std::vector<float> slice(const std::vector<float>& v, int from, int to)
{
    return {v.begin() + from, v.begin() + to};
}

std::vector<float> values(std::initializer_list<int> frames)
{
    std::vector<float> out;
    for (int f : frames) out.push_back(static_cast<float>(f + 1)); // counting() value of a frame
    return out;
}

// Fades are 5 ms: 5 frames at 1 kHz.
constexpr int kFade = 5;

} // namespace

TEST_CASE("PlayHead plays a one-shot forward untouched, then stops", "[playhead]")
{
    MemorySource src(counting(20));
    PlayHead head;
    head.prepare(1000);
    head.start(src, {});
    const Rendered r = render(head, 30);
    CHECK(r.sounding == 20);
    for (int i = 0; i < 20; ++i) CHECK(r.left[static_cast<std::size_t>(i)] == static_cast<float>(i + 1)); // no fades
    CHECK(slice(r.left, 20, 30) == std::vector<float>(10, 0.0f));
    CHECK(r.right == r.left); // mono on both sides
    CHECK_FALSE(head.active());
}

TEST_CASE("PlayHead plays stereo as stereo", "[playhead]")
{
    MemorySource src(counting(10, 1000, 2));
    PlayHead head;
    head.prepare(1000);
    head.start(src, {});
    const Rendered r = render(head, 10);
    CHECK(r.left[3] == 4.0f);
    CHECK(r.right[3] == -4.0f);
}

TEST_CASE("PlayHead plays reversed, fading in where the file ends", "[playhead]")
{
    MemorySource src(counting(20));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.direction = Direction::Reverse;
    head.start(src, o);
    const Rendered r = render(head, 20);
    CHECK(r.sounding == 20);
    for (int i = kFade; i < 20 - kFade; ++i) CHECK(r.left[static_cast<std::size_t>(i)] == static_cast<float>(20 - i));
    CHECK(r.left[0] < r.left[kFade]);                      // faded in
    CHECK(r.left[19] < 1.0f);                              // and out: a reversed attack would click
    CHECK(r.left[0] == Catch::Approx(20.0f / kFade));      // 1/5 of frame 19's value
}

TEST_CASE("PlayHead fades at trim points but not at the file's edges", "[playhead]")
{
    MemorySource src(counting(100));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.trimStart = 10;
    o.trimEnd = 40;
    head.start(src, o);
    const Rendered r = render(head, 40);
    CHECK(r.sounding == 30);
    CHECK(r.left[0] == Catch::Approx(11.0f / kFade)); // frame 10 at 1/5
    CHECK(r.left[kFade] == 16.0f);                    // frame 15, full
    CHECK(r.left[29] == Catch::Approx(40.0f / kFade)); // frame 39, fading out
    CHECK(slice(r.left, 30, 40) == std::vector<float>(10, 0.0f));

    o.trimStart = 0;
    o.trimEnd = -1;
    head.start(src, o);
    const Rendered whole = render(head, 100);
    CHECK(whole.left[0] == 1.0f);
    CHECK(whole.left[99] == 100.0f);
}

TEST_CASE("PlayHead loops an untrimmed forward loop seamlessly", "[playhead]")
{
    MemorySource src(counting(8));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.loop = true;
    head.start(src, o);
    const Rendered r = render(head, 20);
    CHECK(r.sounding == 20);
    for (int i = 0; i < 20; ++i) CHECK(r.left[static_cast<std::size_t>(i)] == static_cast<float>(i % 8 + 1));
    CHECK(head.active());
}

TEST_CASE("PlayHead fades a trimmed loop at its wrap", "[playhead]")
{
    MemorySource src(counting(100));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.loop = true;
    o.trimStart = 20;
    o.trimEnd = 50;
    head.start(src, o);
    const Rendered r = render(head, 90);
    CHECK(r.left[29] == Catch::Approx(50.0f / kFade)); // last frame of a pass, faded
    CHECK(r.left[30] == Catch::Approx(21.0f / kFade)); // first frame of the next
    CHECK(r.left[45] == 36.0f);
}

TEST_CASE("PlayHead ping-pongs without repeating the turning sample", "[playhead]")
{
    MemorySource src(counting(6));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.direction = Direction::PingPong;
    o.loop = true;
    head.start(src, o);
    const Rendered r = render(head, 16);
    // 0 1 2 3 4 5 4 3 2 1 0 1 2 3 4 5: turns are continuous, so no fades.
    CHECK(r.left == values({0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5}));

    o.loop = false;
    head.start(src, o);
    const Rendered once = render(head, 16);
    CHECK(once.sounding == 11); // forward, back, stop
    CHECK(slice(once.left, 0, 6) == values({0, 1, 2, 3, 4, 5}));
    CHECK(slice(once.left, 11, 16) == std::vector<float>(5, 0.0f));
}

TEST_CASE("PlayHead converts sample rates", "[playhead]")
{
    SECTION("upsampling interpolates between frames")
    {
        MemorySource src(counting(100));
        PlayHead head;
        head.prepare(2000);
        head.start(src, {});
        const Rendered r = render(head, 200);
        CHECK(r.sounding == 200);
        CHECK(r.left[20] == Catch::Approx(11.0f));  // frame 10
        CHECK(r.left[21] == Catch::Approx(11.5f));  // halfway to frame 11: exact on a straight line
    }
    SECTION("a 440 Hz tone at 44.1 kHz stays 440 Hz at 48 kHz")
    {
        auto b = std::make_shared<AudioBuffer>();
        b->sampleRate = 44100;
        b->channels.push_back(asma::test::sine(440.0, 1.0, 0.5, 44100));
        MemorySource src(b);
        PlayHead head;
        head.prepare(48000);
        head.start(src, {});
        const Rendered r = render(head, 48000);
        CHECK(r.sounding == Catch::Approx(48000).margin(1));
        int crossings = 0;
        float peak = 0.0f;
        for (std::size_t i = 1; i < 47000; ++i) {
            if ((r.left[i - 1] < 0.0f) != (r.left[i] < 0.0f)) ++crossings;
            peak = std::max(peak, std::abs(r.left[i]));
        }
        CHECK(crossings / 2.0 / (47000.0 / 48000.0) == Catch::Approx(440.0).margin(2.0));
        CHECK(peak == Catch::Approx(0.5).margin(0.01));
    }
    SECTION("speed doubles the pitch and halves the length")
    {
        MemorySource src(counting(100));
        PlayHead head;
        head.prepare(1000);
        PlayOptions o;
        o.speed = 2.0;
        head.start(src, o);
        const Rendered r = render(head, 100);
        CHECK(r.sounding == 50);
        CHECK(r.left[10] == Catch::Approx(21.0f)); // frame 20
    }
}

TEST_CASE("PlayHead ignores an empty region and plays a single frame", "[playhead]")
{
    MemorySource src(counting(10));
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.trimStart = 5;
    o.trimEnd = 5;
    head.start(src, o);
    CHECK_FALSE(head.active());
    CHECK(render(head, 4).sounding == 0);

    o.trimEnd = 6;
    o.direction = Direction::PingPong; // too short to turn: plays forward once
    head.start(src, o);
    CHECK(render(head, 4).sounding == 1);
}

namespace {

struct HintSpy final : SampleSource {
    MemorySource inner{counting(100)};
    std::int64_t frame = -1;
    int direction = 0;
    int sampleRate() const override { return inner.sampleRate(); }
    int channels() const override { return inner.channels(); }
    std::int64_t frames() const override { return inner.frames(); }
    bool read(std::int64_t start, int n, float* const* out) override { return inner.read(start, n, out); }
    void hint(std::int64_t f, int d) override
    {
        frame = f;
        direction = d;
    }
};

} // namespace

TEST_CASE("PlayHead tells the source where it is going", "[playhead]")
{
    HintSpy spy;
    PlayHead head;
    head.prepare(1000);
    PlayOptions o;
    o.direction = Direction::Reverse;
    head.start(spy, o);
    render(head, 10);
    CHECK(spy.frame == 99);
    CHECK(spy.direction == -1);
    render(head, 10);
    CHECK(spy.frame == 89);
}
