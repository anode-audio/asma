// SPDX-License-Identifier: GPL-3.0-only
// Deterministic synthetic audio for analysis tests. Generated, so there is no
// third-party audio in the repo.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace asma::test {

constexpr double kTwoPi = 6.28318530717958647692;

// Uniform noise in -1..1 from a 32-bit LCG.
class Noise {
public:
    explicit Noise(std::uint32_t seed) : state_(seed) {}
    float next()
    {
        state_ = state_ * 1664525u + 1013904223u;
        return static_cast<float>(state_ >> 8) / 8388608.0f - 1.0f;
    }

private:
    std::uint32_t state_;
};

inline std::vector<float> sine(double hz, double seconds, double amplitude, int rate)
{
    std::vector<float> out(static_cast<std::size_t>(std::lround(seconds * rate)));
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<float>(amplitude * std::sin(kTwoPi * hz * static_cast<double>(i) / rate));
    return out;
}

inline std::vector<float> noise(double seconds, double amplitude, int rate, std::uint32_t seed)
{
    Noise n(seed);
    std::vector<float> out(static_cast<std::size_t>(std::lround(seconds * rate)));
    for (auto& x : out) x = static_cast<float>(amplitude) * n.next();
    return out;
}

// Adds a hit starting at `start` (samples), clipped to the buffer.
inline void addKick(std::vector<float>& out, std::size_t start, int rate, double amplitude = 0.8)
{
    double phase = 0.0;
    const auto length = static_cast<std::size_t>(0.35 * rate);
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double hz = 50.0 + 100.0 * std::exp(-t / 0.03); // pitch drop 150 -> 50 Hz
        phase += kTwoPi * hz / rate;
        out[start + i] += static_cast<float>(amplitude * std::exp(-t / 0.08) * std::sin(phase));
    }
}

inline void addSnare(std::vector<float>& out, std::size_t start, int rate, Noise& noise, double amplitude = 0.5)
{
    const auto length = static_cast<std::size_t>(0.2 * rate);
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double body = 0.4 * std::sin(kTwoPi * 190.0 * t);
        out[start + i] += static_cast<float>(amplitude * std::exp(-t / 0.05) * (noise.next() + body));
    }
}

inline void addHat(std::vector<float>& out, std::size_t start, int rate, Noise& noise, double amplitude = 0.2)
{
    const auto length = static_cast<std::size_t>(0.06 * rate);
    float previous = 0.0f;
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const float white = noise.next();
        out[start + i] += static_cast<float>(amplitude * std::exp(-t / 0.015)) * (white - previous); // crude high-pass
        previous = white;
    }
}

// Kick on beats 1 and 3, snare on 2 and 4, hats on eighths, cut to whole bars.
inline std::vector<float> drumLoop(double bpm, int bars, int rate, std::uint32_t seed = 7)
{
    const double beat = 60.0 / bpm;
    std::vector<float> out(static_cast<std::size_t>(std::lround(bars * 4 * beat * rate)));
    Noise noise(seed);
    for (int b = 0; b < bars * 4; ++b) {
        const auto at = static_cast<std::size_t>(std::lround(b * beat * rate));
        if (b % 2 == 0) addKick(out, at, rate);
        else addSnare(out, at, rate, noise);
        addHat(out, at, rate, noise);
        addHat(out, static_cast<std::size_t>(std::lround((b + 0.5) * beat * rate)), rate, noise);
    }
    return out;
}

// A tone with five harmonics at 1/h amplitude.
inline void addTone(std::vector<float>& out, std::size_t start, std::size_t length, double hz, double amplitude,
                    int rate)
{
    for (std::size_t i = 0; i < length && start + i < out.size(); ++i) {
        const double t = static_cast<double>(i) / rate;
        const double fade = std::min(1.0, std::min(t / 0.01, static_cast<double>(length - i) / (0.01 * rate)));
        double v = 0.0;
        for (int h = 1; h <= 5; ++h) v += std::sin(kTwoPi * hz * h * t) / h;
        out[start + i] += static_cast<float>(amplitude * fade * v);
    }
}

// I-IV-V-I (major) or i-iv-V-i (harmonic minor) with a bass note, two
// seconds per chord. tonic: pitch class, 0 = C.
inline std::vector<float> chordProgression(int tonic, bool minor, int rate)
{
    const double root = 130.8128 * std::pow(2.0, tonic / 12.0); // C3 upwards
    const int third = minor ? 3 : 4;
    // Each chord: semitone offsets from the tonic.
    const std::vector<std::vector<int>> chords = {
        {0, third, 7}, {5, 5 + (minor ? 3 : 4), 12}, {7, 11, 14}, {0, third, 7}};
    const auto chordLength = static_cast<std::size_t>(2 * rate);
    std::vector<float> out(chordLength * chords.size());
    for (std::size_t c = 0; c < chords.size(); ++c) {
        const std::size_t start = c * chordLength;
        addTone(out, start, chordLength, root * std::pow(2.0, (chords[c][0] - 12) / 12.0), 0.2, rate);
        for (int semitone : chords[c]) addTone(out, start, chordLength, root * std::pow(2.0, semitone / 12.0), 0.12, rate);
    }
    return out;
}

inline std::vector<float> kickHit(int rate) { std::vector<float> out(static_cast<std::size_t>(0.5 * rate)); addKick(out, 0, rate); return out; }

inline std::vector<float> hatHit(int rate, std::uint32_t seed)
{
    std::vector<float> out(static_cast<std::size_t>(0.25 * rate));
    Noise noise(seed);
    addHat(out, 0, rate, noise, 0.6);
    return out;
}

} // namespace asma::test
