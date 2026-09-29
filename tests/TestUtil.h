// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace asma::test {

namespace fs = std::filesystem;

// A fresh directory under the system temp dir, removed on destruction.
class TempDir {
public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        std::random_device rd;
        path_ = fs::temp_directory_path()
              / ("asma-test-" + std::to_string(rd()) + "-" + std::to_string(counter++));
        fs::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

// Sets an environment variable for the lifetime of the object.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name)
    {
        if (const char* old = std::getenv(name)) old_ = old;
        set(value);
    }
    ~ScopedEnv() { set(old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value ? value : "");
#else
        if (value) setenv(name_.c_str(), value, 1);
        else unsetenv(name_.c_str());
#endif
    }
    std::string name_;
    std::optional<std::string> old_;
};

inline void writeBytes(const fs::path& path, std::string_view bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

inline fs::path fixture(std::string_view name) { return fs::path(ASMA_TEST_FIXTURES) / name; }

namespace detail {

inline void putLe16(std::string& b, std::uint16_t v)
{
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>(v >> 8));
}
inline void putLe32(std::string& b, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
inline void putBe16(std::string& b, std::uint16_t v)
{
    b.push_back(static_cast<char>(v >> 8));
    b.push_back(static_cast<char>(v & 0xFF));
}
inline void putBe32(std::string& b, std::uint32_t v)
{
    for (int i = 3; i >= 0; --i) b.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
inline void putChunkLe(std::string& b, std::string_view id, const std::string& payload)
{
    b.append(id.data(), 4);
    putLe32(b, static_cast<std::uint32_t>(payload.size()));
    b += payload;
    if (payload.size() & 1) b.push_back('\0');
}
inline void putChunkBe(std::string& b, std::string_view id, const std::string& payload)
{
    b.append(id.data(), 4);
    putBe32(b, static_cast<std::uint32_t>(payload.size()));
    b += payload;
    if (payload.size() & 1) b.push_back('\0');
}
// Deterministic pseudo-random sample bytes; different seeds give different data.
inline std::string sampleBytes(std::size_t count, std::uint32_t seed)
{
    std::string data;
    data.reserve(count);
    std::uint32_t x = seed;
    for (std::size_t i = 0; i < count; ++i) {
        x = x * 1664525u + 1013904223u;
        data.push_back(static_cast<char>((x >> 24) & 0xFF));
    }
    return data;
}
// 80-bit IEEE extended, as used for the AIFF sample rate.
inline std::string extended(double value)
{
    int exponent = 0;
    const double mantissa = std::frexp(value, &exponent);
    const auto bits = static_cast<std::uint64_t>(std::ldexp(mantissa, 64));
    const int biased = exponent - 1 + 16383;
    std::string out;
    out.push_back(static_cast<char>((biased >> 8) & 0x7F));
    out.push_back(static_cast<char>(biased & 0xFF));
    for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
    return out;
}

} // namespace detail

struct WavSpec {
    int sampleRate = 44100;
    int channels = 1;
    int bitsPerSample = 16;
    int frames = 4410;
    std::uint32_t seed = 1;
    std::optional<std::pair<bool, float>> acid; // {oneShot, tempo}
    std::optional<int> smplUnityNote;
    std::vector<std::pair<std::string, std::string>> chunksBeforeData; // {4-char id, payload}
    std::uint16_t formatTag = 1; // 1 = PCM; anything else makes an undecodable file
};

inline void writeWav(const fs::path& path, const WavSpec& spec)
{
    using namespace detail;
    const int blockAlign = spec.channels * spec.bitsPerSample / 8;
    std::string fmt;
    putLe16(fmt, spec.formatTag);
    putLe16(fmt, static_cast<std::uint16_t>(spec.channels));
    putLe32(fmt, static_cast<std::uint32_t>(spec.sampleRate));
    putLe32(fmt, static_cast<std::uint32_t>(spec.sampleRate * blockAlign));
    putLe16(fmt, static_cast<std::uint16_t>(blockAlign));
    putLe16(fmt, static_cast<std::uint16_t>(spec.bitsPerSample));

    std::string body = "WAVE";
    putChunkLe(body, "fmt ", fmt);
    if (spec.acid) {
        std::string acid;
        putLe32(acid, spec.acid->first ? 1u : 0u); // flags: bit 0 = one-shot
        putLe16(acid, 0x3C);                      // root note
        putLe16(acid, 0x8000);
        putLe32(acid, 0);                         // unknown float
        putLe32(acid, 8);                         // beats
        putLe16(acid, 4);                         // meter denominator
        putLe16(acid, 4);                         // meter numerator
        std::uint32_t tempoBits = 0;
        std::memcpy(&tempoBits, &spec.acid->second, 4);
        putLe32(acid, tempoBits);
        putChunkLe(body, "acid", acid);
    }
    if (spec.smplUnityNote) {
        std::string smpl;
        putLe32(smpl, 0);     // manufacturer
        putLe32(smpl, 0);     // product
        putLe32(smpl, 22675); // sample period
        putLe32(smpl, static_cast<std::uint32_t>(*spec.smplUnityNote));
        for (int i = 0; i < 5; ++i) putLe32(smpl, 0);
        putChunkLe(body, "smpl", smpl);
    }
    for (const auto& [id, payload] : spec.chunksBeforeData) putChunkLe(body, id, payload);
    putChunkLe(body, "data",
               sampleBytes(static_cast<std::size_t>(spec.frames) * static_cast<std::size_t>(blockAlign), spec.seed));

    std::string file = "RIFF";
    putLe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}

// 16-bit mono PCM WAV of the given samples (clipped to -1..1).
inline void writeWavSamples(const fs::path& path, int sampleRate, const std::vector<float>& samples)
{
    using namespace detail;
    std::string fmt;
    putLe16(fmt, 1);
    putLe16(fmt, 1);
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate));
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate * 2));
    putLe16(fmt, 2);
    putLe16(fmt, 16);
    std::string data;
    data.reserve(samples.size() * 2);
    for (float s : samples) {
        const float clipped = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s);
        putLe16(data, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(clipped * 32767.0f))));
    }
    std::string body = "WAVE";
    putChunkLe(body, "fmt ", fmt);
    putChunkLe(body, "data", data);
    std::string file = "RIFF";
    putLe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}

// 32-bit float WAV, one vector per channel (all the same length). Float keeps
// every value exact, so playback tests can compare samples one for one.
inline void writeWavFloat(const fs::path& path, int sampleRate, const std::vector<std::vector<float>>& channels)
{
    using namespace detail;
    const auto count = static_cast<std::uint16_t>(channels.size());
    std::string fmt;
    putLe16(fmt, 3); // IEEE float
    putLe16(fmt, count);
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate));
    putLe32(fmt, static_cast<std::uint32_t>(sampleRate * 4 * count));
    putLe16(fmt, static_cast<std::uint16_t>(4 * count));
    putLe16(fmt, 32);
    std::string data;
    const std::size_t frames = channels.empty() ? 0 : channels[0].size();
    data.reserve(frames * 4 * count);
    for (std::size_t i = 0; i < frames; ++i)
        for (const auto& channel : channels) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &channel[i], 4);
            putLe32(data, bits);
        }
    std::string body = "WAVE";
    putChunkLe(body, "fmt ", fmt);
    putChunkLe(body, "data", data);
    std::string file = "RIFF";
    putLe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}

// 0, 1, 2, ... n-1 scaled by `step`: every frame is recognisable by value.
inline std::vector<float> ramp(std::size_t n, float step = 1.0f / 65536.0f, float offset = 0.0f)
{
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) out[i] = offset + static_cast<float>(i) * step;
    return out;
}

struct AiffSpec {
    int sampleRate = 44100;
    int channels = 1;
    int bitsPerSample = 16;
    int frames = 4410;
    std::uint32_t seed = 1;
};

inline void writeAiff(const fs::path& path, const AiffSpec& spec)
{
    using namespace detail;
    std::string comm;
    putBe16(comm, static_cast<std::uint16_t>(spec.channels));
    putBe32(comm, static_cast<std::uint32_t>(spec.frames));
    putBe16(comm, static_cast<std::uint16_t>(spec.bitsPerSample));
    comm += extended(spec.sampleRate);

    std::string ssnd;
    putBe32(ssnd, 0); // offset
    putBe32(ssnd, 0); // block size
    ssnd += sampleBytes(static_cast<std::size_t>(spec.frames) * static_cast<std::size_t>(spec.channels)
                            * static_cast<std::size_t>(spec.bitsPerSample / 8),
                        spec.seed);

    std::string body = "AIFF";
    putChunkBe(body, "COMM", comm);
    putChunkBe(body, "SSND", ssnd);
    std::string file = "FORM";
    putBe32(file, static_cast<std::uint32_t>(body.size()));
    file += body;
    writeBytes(path, file);
}

} // namespace asma::test
