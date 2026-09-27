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

// ---- Descriptors, loop or one-shot, and the full analysis

namespace {

constexpr double kPi = 3.14159265358979323846;

double onsetsPerSecond(const Onsets& onsets, double seconds)
{
    const auto& e = onsets.envelope;
    if (e.size() < 3 || seconds <= 0.0) return 0.0;
    const float peak = *std::max_element(e.begin(), e.end());
    if (peak <= 0.0f) return 0.0;
    const auto minGap = static_cast<std::size_t>(std::max(1.0, onsets.frameRate * 0.05));
    std::size_t count = 0;
    std::size_t last = 0;
    bool any = false;
    for (std::size_t t = 1; t + 1 < e.size(); ++t) {
        if (e[t] > 0.1f * peak && e[t] > e[t - 1] && e[t] >= e[t + 1] && (!any || t - last >= minGap)) {
            ++count;
            last = t;
            any = true;
        }
    }
    return static_cast<double>(count) / seconds;
}

struct Spectral {
    double centroid = 0.0;
    double rolloff = 0.0;
    double flatness = 0.0;
    std::array<double, 13> mfccMean{};
    std::array<double, 13> mfccStd{};
};

double hzToMel(double hz) { return 2595.0 * std::log10(1.0 + hz / 700.0); }
double melToHz(double mel) { return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0); }

Spectral spectralSummary(const std::vector<float>& mono, int rate)
{
    const std::size_t size = detail::powerOfTwoAtLeast(rate * 0.046);
    const auto frames = detail::stft(mono, size, size / 4);
    const std::size_t bins = size / 2 + 1;
    const double binHz = static_cast<double>(rate) / static_cast<double>(size);

    // 40 triangular mel bands between 20 Hz and min(Nyquist, 16 kHz).
    constexpr int kBands = 40;
    const double melLo = hzToMel(20.0);
    const double melHi = hzToMel(std::min(rate / 2.0, 16000.0));
    std::vector<std::vector<std::pair<std::size_t, double>>> bands(kBands);
    for (int b = 0; b < kBands; ++b) {
        const double lo = melToHz(melLo + (melHi - melLo) * b / (kBands + 1));
        const double mid = melToHz(melLo + (melHi - melLo) * (b + 1) / (kBands + 1));
        const double hi = melToHz(melLo + (melHi - melLo) * (b + 2) / (kBands + 1));
        for (std::size_t k = 1; k < bins; ++k) {
            const double f = static_cast<double>(k) * binHz;
            if (f <= lo || f >= hi) continue;
            bands[static_cast<std::size_t>(b)].emplace_back(k, f < mid ? (f - lo) / (mid - lo) : (hi - f) / (hi - mid));
        }
    }

    std::vector<double> energy(frames.size(), 0.0);
    for (std::size_t t = 0; t < frames.size(); ++t)
        for (float m : frames[t]) energy[t] += static_cast<double>(m) * m;
    const double maxEnergy = *std::max_element(energy.begin(), energy.end());

    Spectral s;
    if (maxEnergy <= 0.0) return s;
    std::array<double, 13> sum{};
    std::array<double, 13> sumSq{};
    std::size_t active = 0;
    std::vector<double> power(bins);
    for (std::size_t t = 0; t < frames.size(); ++t) {
        if (energy[t] < maxEnergy * 1e-6) continue; // quieter than -60 dB: skip
        ++active;
        double total = 0.0;
        double weighted = 0.0;
        double logSum = 0.0;
        for (std::size_t k = 0; k < bins; ++k) {
            power[k] = static_cast<double>(frames[t][k]) * frames[t][k];
            total += power[k];
            weighted += power[k] * static_cast<double>(k) * binHz;
            logSum += std::log(power[k] + 1e-12);
        }
        s.centroid += weighted / total;
        double cumulative = 0.0;
        std::size_t k85 = 0;
        while (k85 < bins && cumulative < 0.85 * total) cumulative += power[k85++];
        s.rolloff += static_cast<double>(k85) * binHz;
        s.flatness += std::exp(logSum / static_cast<double>(bins)) / (total / static_cast<double>(bins) + 1e-12);

        std::array<double, kBands> logBand{};
        for (int b = 0; b < kBands; ++b) {
            double e = 0.0;
            for (const auto& [k, w] : bands[static_cast<std::size_t>(b)]) e += w * power[k];
            logBand[static_cast<std::size_t>(b)] = std::log10(e + 1e-10);
        }
        for (int n = 0; n < 13; ++n) {
            double c = 0.0;
            for (int b = 0; b < kBands; ++b)
                c += logBand[static_cast<std::size_t>(b)] * std::cos(kPi * n * (b + 0.5) / kBands);
            sum[static_cast<std::size_t>(n)] += c;
            sumSq[static_cast<std::size_t>(n)] += c * c;
        }
    }
    const auto count = static_cast<double>(active);
    s.centroid /= count;
    s.rolloff /= count;
    s.flatness /= count;
    for (std::size_t n = 0; n < 13; ++n) {
        s.mfccMean[n] = sum[n] / count;
        s.mfccStd[n] = std::sqrt(std::max(0.0, sumSq[n] / count - s.mfccMean[n] * s.mfccMean[n]));
    }
    return s;
}

// Share of the sound's energy in its first quarter. One-shots are front-loaded;
// loops spread their energy across their length.
double frontEnergyShare(const std::vector<float>& mono)
{
    double total = 0.0;
    double front = 0.0;
    for (std::size_t i = 0; i < mono.size(); ++i) {
        const double e = static_cast<double>(mono[i]) * mono[i];
        total += e;
        if (i < mono.size() / 4) front += e;
    }
    return total > 0.0 ? front / total : 0.0;
}

// True when the last tenth of the sound is 40 dB below its loudest 50 ms.
bool decaysToSilence(const std::vector<float>& mono, int rate)
{
    const auto window = static_cast<std::size_t>(std::max(1, rate / 20));
    double loudest = 0.0;
    for (std::size_t start = 0; start < mono.size(); start += window) {
        double sum = 0.0;
        const std::size_t end = std::min(mono.size(), start + window);
        for (std::size_t i = start; i < end; ++i) sum += static_cast<double>(mono[i]) * mono[i];
        loudest = std::max(loudest, sum / static_cast<double>(end - start));
    }
    const std::size_t tailStart = mono.size() - mono.size() / 10;
    double tail = 0.0;
    for (std::size_t i = tailStart; i < mono.size(); ++i) tail += static_cast<double>(mono[i]) * mono[i];
    tail /= static_cast<double>(std::max<std::size_t>(1, mono.size() - tailStart));
    return loudest > 0.0 && tail < loudest * 1e-4;
}

// Autocorrelation at a fractional lag, by linear interpolation.
double autocorrelationAt(const std::vector<float>& e, double lag)
{
    const auto lo = static_cast<std::size_t>(std::floor(lag));
    const double frac = lag - static_cast<double>(lo);
    return (1.0 - frac) * autocorrelation(e, lo) + frac * autocorrelation(e, lo + 1);
}

// Loops are cut to whole bars, so their length allows only a few tempos:
// 240 * bars / seconds. Score each by its autocorrelation (weighted towards
// 120 BPM), metre-check the winner, and keep the result on a whole-bar tempo.
std::optional<TempoEstimate> barFitTempo(const OnsetPair& pair, double seconds)
{
    const BeatEnvelope b = beatEnvelope(pair);
    if (b.energy <= 0.0 || seconds <= 0.0) return std::nullopt;

    int bestBars = 0;
    double bestScore = 0.0;
    double bestCorrelation = 0.0;
    for (int bars = 1; bars <= 512; ++bars) {
        const double bpm = 240.0 * bars / seconds;
        if (bpm < kMinBpm) continue;
        if (bpm > kMaxBpm) break;
        const double lag = 60.0 * b.frameRate / bpm;
        if (lag + 1.0 >= static_cast<double>(b.e.size())) continue;
        const double correlation = autocorrelationAt(b.centered, lag) / b.energy;
        const double score = correlation * tempoPrior(bpm);
        if (score > bestScore) {
            bestScore = score;
            bestBars = bars;
            bestCorrelation = correlation;
        }
    }
    if (bestBars == 0) return std::nullopt;

    int bars = bestBars;
    const double factor = metreFactor(b, 60.0 * b.frameRate / (240.0 * bars / seconds));
    if (factor == 0.5 && bars % 2 == 0 && 240.0 * (bars / 2) / seconds >= kMinBpm) bars /= 2;
    if (factor == 2.0 && 240.0 * (bars * 2) / seconds <= kMaxBpm) bars *= 2;

    TempoEstimate estimate;
    estimate.bpm = 240.0 * bars / seconds;
    estimate.confidence = std::clamp(bestCorrelation, 0.0, 1.0);
    return estimate;
}

} // namespace

AnalysisResult analyse(const DecodedAudio& audio)
{
    AnalysisResult r;
    const int rate = audio.sampleRate;
    const double seconds = audio.seconds();
    r.loudness = measureLoudness(audio.mono, rate);

    const Spectral spectral = spectralSummary(audio.mono, rate);
    r.centroid = spectral.centroid;
    r.rolloff = spectral.rolloff;
    r.flatness = spectral.flatness;
    const OnsetPair onsets = onsetEnvelopes(audio.mono, rate);
    r.onsetDensity = onsetsPerSecond(onsets.full, seconds);

    if (audio.truncated) {
        // Only the first maxSeconds were decoded, so the length says nothing.
        const auto tempo = tempoFromOnsets(onsets);
        if (tempo && tempo->confidence >= 0.4) {
            r.bpm = tempo->bpm;
            r.bpmConfidence = tempo->confidence;
        }
    } else if (seconds < 1.0) {
        r.isLoop = false;
    } else {
        // Thresholds tuned on labelled loops and one-shots from a real sample
        // library: 75% of loops found, 13% of one-shots over a second mistaken.
        const auto tempo = barFitTempo(onsets, seconds);
        if (tempo && tempo->confidence >= 0.15 && frontEnergyShare(audio.mono) < 0.6) {
            r.bpm = tempo->bpm;
            r.bpmConfidence = tempo->confidence;
            r.isLoop = true;
        } else if (decaysToSilence(audio.mono, rate)) {
            r.isLoop = false;
        }
    }

    if (const auto key = estimateKey(audio.mono, rate)) {
        r.key = key->key;
        r.keyConfidence = key->confidence;
    }

    r.featureVector.reserve(kFeatureVectorSize);
    for (double v : spectral.mfccMean) r.featureVector.push_back(static_cast<float>(v));
    for (double v : spectral.mfccStd) r.featureVector.push_back(static_cast<float>(v));
    r.featureVector.push_back(static_cast<float>(std::log(r.centroid + 1.0)));
    r.featureVector.push_back(static_cast<float>(std::log(r.rolloff + 1.0)));
    r.featureVector.push_back(static_cast<float>(r.flatness));
    r.featureVector.push_back(static_cast<float>(std::log1p(r.onsetDensity)));
    r.featureVector.push_back(static_cast<float>(std::log(seconds + 0.01)));
    return r;
}

} // namespace asma
