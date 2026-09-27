// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Analysis.h"

#include "Fft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

#include <ebur128.h>

namespace asma {

// ---- Loudness

Loudness measureLoudness(const std::vector<float>& mono, int sampleRate)
{
    Loudness result;
    if (mono.empty() || sampleRate <= 0) return result;
    ebur128_state* state =
        ebur128_init(1, static_cast<unsigned long>(sampleRate), EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK);
    if (!state) return result;
    const auto durationMs =
        std::max<unsigned long>(1, static_cast<unsigned long>(mono.size() * 1000 / static_cast<std::size_t>(sampleRate)));
    ebur128_add_frames_float(state, mono.data(), mono.size());

    double lufs = -HUGE_VAL;
    ebur128_loudness_global(state, &lufs);
    // Sounds under 400 ms are too short to gate: measure them ungated over
    // their whole length, which the default 400 ms window still holds.
    if (!std::isfinite(lufs) && durationMs <= 400) ebur128_loudness_window(state, durationMs, &lufs);
    double peak = 0.0;
    ebur128_sample_peak(state, 0, &peak);
    ebur128_destroy(&state);

    result.peak = peak;
    result.lufs = std::isfinite(lufs) ? std::max(lufs, -70.0) : -70.0;
    return result;
}

// ---- Tempo

namespace {

struct Onsets {
    std::vector<float> envelope; // positive spectral flux above its local mean
    double frameRate = 0.0;      // envelope frames per second
};

constexpr double kLowHz = 1500.0; // kick and snare bodies; hats sit above this

struct OnsetPair {
    Onsets full; // all bins
    Onsets low;  // bins up to kLowHz
};

// Positive spectral flux above its local mean (~0.25 s), from one list of
// frames, over the first `bins` bins.
std::vector<float> fluxEnvelope(const std::vector<std::vector<float>>& frames, std::size_t bins, double frameRate)
{
    std::vector<float> flux(frames.size(), 0.0f);
    std::vector<float> previous(bins, 0.0f);
    for (std::size_t t = 0; t < frames.size(); ++t) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < bins; ++k) {
            const float value = std::log1p(100.0f * frames[t][k]);
            if (t > 0 && value > previous[k]) sum += value - previous[k];
            previous[k] = value;
        }
        flux[t] = sum;
    }
    const auto half = static_cast<std::size_t>(std::max(1.0, std::round(frameRate * 0.125)));
    std::vector<double> prefix(flux.size() + 1, 0.0);
    for (std::size_t t = 0; t < flux.size(); ++t) prefix[t + 1] = prefix[t] + flux[t];
    std::vector<float> envelope(flux.size());
    for (std::size_t t = 0; t < flux.size(); ++t) {
        const std::size_t lo = t >= half ? t - half : 0;
        const std::size_t hi = std::min(flux.size(), t + half + 1);
        const double mean = (prefix[hi] - prefix[lo]) / static_cast<double>(hi - lo);
        envelope[t] = static_cast<float>(std::max(0.0, flux[t] - mean));
    }
    return envelope;
}

// Log-magnitude spectral flux on ~23 ms frames with a 4x overlap.
OnsetPair onsetEnvelopes(const std::vector<float>& mono, int rate)
{
    const std::size_t size = detail::powerOfTwoAtLeast(rate * 0.023);
    const std::size_t hop = size / 4;
    const auto frames = detail::stft(mono, size, hop);
    const double frameRate = static_cast<double>(rate) / static_cast<double>(hop);
    const std::size_t bins = frames.front().size();
    const std::size_t lowBins = std::min(bins, static_cast<std::size_t>(kLowHz * static_cast<double>(size) / rate) + 1);
    OnsetPair pair;
    pair.full = {fluxEnvelope(frames, bins, frameRate), frameRate};
    pair.low = {fluxEnvelope(frames, lowBins, frameRate), frameRate};
    return pair;
}

// Onsets are one-frame spikes; smoothing lets beats that fall between frames
// still line up at integer lags.

// Onsets are one-frame spikes; smoothing lets beats that fall between frames
// still line up at integer lags.
std::vector<float> smoothed(const std::vector<float>& raw)
{
    static constexpr std::array<float, 5> kSmooth = {0.1f, 0.2f, 0.4f, 0.2f, 0.1f};
    std::vector<float> e(raw.size(), 0.0f);
    for (std::size_t t = 0; t < raw.size(); ++t)
        for (std::size_t j = 0; j < kSmooth.size(); ++j)
            if (t + j >= 2 && t + j - 2 < raw.size()) e[t] += kSmooth[j] * raw[t + j - 2];
    return e;
}

double autocorrelation(const std::vector<float>& e, std::size_t lag)
{
    if (lag >= e.size()) return 0.0;
    double sum = 0.0;
    for (std::size_t t = 0; t + lag < e.size(); ++t) sum += static_cast<double>(e[t]) * e[t + lag];
    return sum / static_cast<double>(e.size() - lag);
}

constexpr double kMinBpm = 55.0;
constexpr double kMaxBpm = 210.0;

double tempoPrior(double bpm)
{
    const double octaves = std::log2(bpm / 120.0);
    return std::exp(-0.5 * octaves * octaves); // log-Gaussian around 120, one octave wide
}

struct BeatEnvelope {
    std::vector<float> e;        // smoothed onset envelope, for pulse strengths
    std::vector<float> centered; // e minus its mean, for autocorrelation
    double frameRate = 0.0;
    double energy = 0.0;         // autocorrelation of `centered` at lag 0
};

// Beats are judged on onsets below 1.5 kHz, where kicks and snares live and
// hats barely register; otherwise eighth-note hats look like beats. Material
// with nothing down there (shaker loops) falls back to all bins.
BeatEnvelope beatEnvelope(const OnsetPair& pair)
{
    const double lowSum = std::accumulate(pair.low.envelope.begin(), pair.low.envelope.end(), 0.0);
    const double fullSum = std::accumulate(pair.full.envelope.begin(), pair.full.envelope.end(), 0.0);
    const Onsets& onsets = lowSum < 0.05 * fullSum ? pair.full : pair.low;
    BeatEnvelope b;
    b.e = smoothed(onsets.envelope);
    b.frameRate = onsets.frameRate;
    // The envelope is never negative, so its raw autocorrelation is high at
    // every lag. Centred, periodic pulses still peak while a single hit or a
    // noisy tail averages out.
    const double mean = b.e.empty() ? 0.0 : std::accumulate(b.e.begin(), b.e.end(), 0.0) / static_cast<double>(b.e.size());
    b.centered.resize(b.e.size());
    for (std::size_t i = 0; i < b.e.size(); ++i) b.centered[i] = static_cast<float>(b.e[i] - mean);
    b.energy = autocorrelation(b.centered, 0);
    return b;
}

// Metre check at a beat period (frames). Pulses aligned to the strongest
// phase: if every other pulse is weak (hats between kicks and snares), the
// beat is twice as slow (returns 0.5); if the midpoints are as strong as the
// pulses, twice as fast (returns 2). Otherwise 1.
double metreFactor(const BeatEnvelope& b, double period)
{
    const auto& e = b.e;
    auto strengthAt = [&](double position) {
        const auto i = static_cast<std::size_t>(std::lround(position));
        float m = 0.0f;
        for (std::size_t j = i >= 1 ? i - 1 : 0; j <= i + 1 && j < e.size(); ++j) m = std::max(m, e[j]);
        return static_cast<double>(m);
    };
    auto pulses = [&](double phase, int parity) { // parity: -1 all, 0 even, 1 odd
        double sum = 0.0;
        int count = 0;
        int index = 0;
        for (double x = phase; x < static_cast<double>(e.size()); x += period, ++index) {
            if (parity >= 0 && index % 2 != parity) continue;
            sum += strengthAt(x);
            ++count;
        }
        return count > 0 ? sum / count : 0.0;
    };
    double phase = 0.0;
    double phaseScore = -1.0;
    for (double p = 0.0; p < period; p += 1.0) {
        const double s = pulses(p, -1);
        if (s > phaseScore) {
            phaseScore = s;
            phase = p;
        }
    }
    const double even = pulses(phase, 0);
    const double odd = pulses(phase, 1);
    const double middle = pulses(phase + period / 2.0, -1);
    if (std::min(even, odd) < 0.5 * std::max(even, odd)) return 0.5;
    if (middle >= 0.6 * std::min(even, odd)) return 2.0;
    return 1.0;
}

// Free tempo estimate: the autocorrelation peak between 55 and 210 BPM,
// weighted towards 120, refined to sub-frame precision, then metre-checked.
std::optional<TempoEstimate> tempoFromOnsets(const OnsetPair& pair)
{
    const BeatEnvelope b = beatEnvelope(pair);
    if (b.energy <= 0.0) return std::nullopt;
    const auto lagMin = static_cast<std::size_t>(std::floor(60.0 * b.frameRate / kMaxBpm));
    const auto lagMax = std::min(static_cast<std::size_t>(std::ceil(60.0 * b.frameRate / kMinBpm)), b.centered.size() / 2);
    if (lagMin < 2 || lagMax <= lagMin + 2) return std::nullopt;

    std::size_t best = 0;
    double bestScore = 0.0;
    for (std::size_t lag = lagMin; lag <= lagMax; ++lag) {
        const double score = autocorrelation(b.centered, lag) * tempoPrior(60.0 * b.frameRate / static_cast<double>(lag));
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    if (best == 0) return std::nullopt;

    // Parabolic interpolation around the peak.
    const double y0 = autocorrelation(b.centered, best - 1);
    const double y1 = autocorrelation(b.centered, best);
    const double y2 = autocorrelation(b.centered, best + 1);
    const double denominator = y0 - 2.0 * y1 + y2;
    const double offset = denominator < 0.0 ? std::clamp(0.5 * (y0 - y2) / denominator, -0.5, 0.5) : 0.0;
    const double period = static_cast<double>(best) + offset;

    double bpm = 60.0 * b.frameRate / period;
    const double factor = metreFactor(b, period);
    if (bpm * factor >= kMinBpm && bpm * factor <= kMaxBpm) bpm *= factor;

    TempoEstimate estimate;
    estimate.bpm = bpm;
    estimate.confidence = std::clamp(y1 / b.energy, 0.0, 1.0);
    return estimate;
}

} // namespace

std::optional<TempoEstimate> estimateTempo(const std::vector<float>& mono, int sampleRate)
{
    if (sampleRate <= 0 || mono.size() < static_cast<std::size_t>(sampleRate)) return std::nullopt;
    return tempoFromOnsets(onsetEnvelopes(mono, sampleRate));
}

// ---- Key

std::optional<KeyEstimate> estimateKey(const std::vector<float>& mono, int sampleRate)
{
    if (sampleRate <= 0 || mono.size() < static_cast<std::size_t>(sampleRate / 4)) return std::nullopt;
    const std::size_t size = detail::powerOfTwoAtLeast(sampleRate * 0.37); // ~2.7 Hz bins at 44.1 kHz
    const auto frames = detail::stft(mono, size, size / 2);
    const double binHz = static_cast<double>(sampleRate) / static_cast<double>(size);

    std::vector<std::pair<std::size_t, int>> binClass; // bin -> pitch class, 100 Hz .. 5 kHz
    for (std::size_t k = 1; k < size / 2 + 1; ++k) {
        const double f = static_cast<double>(k) * binHz;
        if (f < 100.0 || f > 5000.0) continue;
        const long midi = std::lround(69.0 + 12.0 * std::log2(f / 440.0));
        binClass.emplace_back(k, static_cast<int>(((midi % 12) + 12) % 12));
    }
    if (binClass.empty()) return std::nullopt;

    std::array<double, 12> chroma{};
    double flatnessSum = 0.0;
    double weightSum = 0.0;
    for (const auto& frame : frames) {
        double total = 0.0;
        double logSum = 0.0;
        for (const auto& [k, pc] : binClass) {
            const double p = static_cast<double>(frame[k]) * frame[k];
            total += p;
            logSum += std::log(p + 1e-12);
            chroma[static_cast<std::size_t>(pc)] += frame[k];
        }
        if (total <= 0.0) continue;
        const double mean = total / static_cast<double>(binClass.size());
        flatnessSum += total * std::exp(logSum / static_cast<double>(binClass.size())) / (mean + 1e-12);
        weightSum += total;
    }
    if (weightSum <= 0.0) return std::nullopt;
    if (flatnessSum / weightSum > 0.3) return std::nullopt; // noise-like: no key

    // Temperley-Kostka-Payne key profiles.
    static constexpr std::array<double, 12> kMajor = {0.748, 0.060, 0.488, 0.082, 0.670, 0.460,
                                                      0.096, 0.715, 0.104, 0.366, 0.057, 0.400};
    static constexpr std::array<double, 12> kMinor = {0.712, 0.084, 0.474, 0.618, 0.049, 0.460,
                                                      0.105, 0.747, 0.404, 0.067, 0.133, 0.330};
    static constexpr std::array<const char*, 12> kNames = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

    auto correlate = [&](const std::array<double, 12>& profile, int tonic) {
        double mx = 0.0;
        double my = 0.0;
        for (int i = 0; i < 12; ++i) {
            mx += chroma[static_cast<std::size_t>(i)];
            my += profile[static_cast<std::size_t>((i - tonic + 12) % 12)];
        }
        mx /= 12.0;
        my /= 12.0;
        double sxy = 0.0;
        double sxx = 0.0;
        double syy = 0.0;
        for (int i = 0; i < 12; ++i) {
            const double x = chroma[static_cast<std::size_t>(i)] - mx;
            const double y = profile[static_cast<std::size_t>((i - tonic + 12) % 12)] - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0.0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };

    double best = -2.0;
    double second = -2.0;
    std::string bestKey;
    for (int tonic = 0; tonic < 12; ++tonic) {
        for (bool minor : {false, true}) {
            const double r = correlate(minor ? kMinor : kMajor, tonic);
            if (r > best) {
                second = best;
                best = r;
                bestKey = std::string(kNames[static_cast<std::size_t>(tonic)]) + (minor ? "m" : "");
            } else if (r > second) {
                second = r;
            }
        }
    }
    if (best < 0.5) return std::nullopt;
    KeyEstimate estimate;
    estimate.key = bestKey;
    estimate.confidence = std::clamp((best - second) / 0.2, 0.0, 1.0) * std::clamp(best, 0.0, 1.0);
    return estimate;
}

} // namespace asma
