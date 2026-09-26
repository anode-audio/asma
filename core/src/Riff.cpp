// SPDX-License-Identifier: GPL-3.0-only
#include "Riff.h"

#include "asma/core/Fs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace asma::detail {

namespace {

std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::uint32_t le32(const unsigned char* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t be16(const unsigned char* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

std::uint32_t be32(const unsigned char* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

float leFloat(const unsigned char* p)
{
    const std::uint32_t bits = le32(p);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

// 80-bit IEEE 754 extended precision, big-endian.
double extendedToDouble(const unsigned char* b)
{
    const int exponent = ((b[0] & 0x7F) << 8) | b[1];
    std::uint64_t mantissa = 0;
    for (int i = 2; i < 10; ++i) mantissa = (mantissa << 8) | b[i];
    if (exponent == 0 && mantissa == 0) return 0.0;
    const double value = std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
    return (b[0] & 0x80) ? -value : value;
}

bool readExact(std::FILE* file, void* buffer, std::size_t size)
{
    return std::fread(buffer, 1, size, file) == size;
}

} // namespace

ProbeResult probeWav(std::FILE* file, std::uint64_t fileSize)
{
    unsigned char header[12];
    if (!readExact(file, header, sizeof header)) throw ProbeError("file too short for a WAV header");
    if (std::memcmp(header, "RF64", 4) == 0) throw ProbeError("RF64 WAV files are not supported");
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0)
        throw ProbeError("not a RIFF/WAVE file");

    ProbeResult r;
    r.format = AudioFormat::Wav;
    bool haveFmt = false;
    bool haveData = false;
    int blockAlign = 0;

    std::uint64_t pos = 12;
    while (pos + 8 <= fileSize) {
        unsigned char chunk[8];
        if (!seekFile(file, pos) || !readExact(file, chunk, sizeof chunk)) break;
        const std::uint32_t size = le32(chunk + 4);
        const std::uint64_t payload = pos + 8;

        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            unsigned char fmt[16];
            if (!readExact(file, fmt, sizeof fmt)) throw ProbeError("truncated fmt chunk");
            r.channels = le16(fmt + 2);
            r.sampleRate = static_cast<int>(le32(fmt + 4));
            blockAlign = le16(fmt + 12);
            r.bitDepth = le16(fmt + 14);
            haveFmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            r.hashOffset = payload;
            // Streamed WAVs may carry a bogus size; never run past the file.
            r.hashLength = std::min<std::uint64_t>(size, fileSize - payload);
            haveData = true;
        } else if (std::memcmp(chunk, "acid", 4) == 0 && size >= 24) {
            unsigned char acid[24];
            if (readExact(file, acid, sizeof acid)) {
                AcidInfo info;
                info.oneShot = (le32(acid) & 0x01u) != 0;
                info.tempo = leFloat(acid + 20);
                r.acid = info;
            }
        } else if (std::memcmp(chunk, "smpl", 4) == 0 && size >= 16) {
            unsigned char smpl[16];
            if (readExact(file, smpl, sizeof smpl)) {
                const std::uint32_t note = le32(smpl + 12);
                if (note <= 127) r.smplUnityNote = static_cast<int>(note);
            }
        }
        pos = payload + size + (size & 1u);
    }

    if (!haveFmt) throw ProbeError("WAV file has no fmt chunk");
    if (!haveData) throw ProbeError("WAV file has no data chunk");
    if (r.sampleRate <= 0 || r.channels <= 0 || blockAlign <= 0) throw ProbeError("WAV fmt chunk is invalid");
    r.durationSeconds = static_cast<double>(r.hashLength / static_cast<std::uint64_t>(blockAlign))
                      / static_cast<double>(r.sampleRate);
    return r;
}

ProbeResult probeAiff(std::FILE* file, std::uint64_t fileSize)
{
    unsigned char header[12];
    if (!readExact(file, header, sizeof header)) throw ProbeError("file too short for an AIFF header");
    if (std::memcmp(header, "FORM", 4) != 0
        || (std::memcmp(header + 8, "AIFF", 4) != 0 && std::memcmp(header + 8, "AIFC", 4) != 0))
        throw ProbeError("not an AIFF file");

    ProbeResult r;
    r.format = AudioFormat::Aiff;
    bool haveComm = false;
    bool haveSsnd = false;
    std::uint32_t frames = 0;
    double rate = 0.0;

    std::uint64_t pos = 12;
    while (pos + 8 <= fileSize) {
        unsigned char chunk[8];
        if (!seekFile(file, pos) || !readExact(file, chunk, sizeof chunk)) break;
        const std::uint32_t size = be32(chunk + 4);
        const std::uint64_t payload = pos + 8;

        if (std::memcmp(chunk, "COMM", 4) == 0 && size >= 18) {
            unsigned char comm[18];
            if (!readExact(file, comm, sizeof comm)) throw ProbeError("truncated COMM chunk");
            r.channels = static_cast<std::int16_t>(be16(comm));
            frames = be32(comm + 2);
            r.bitDepth = static_cast<std::int16_t>(be16(comm + 6));
            rate = extendedToDouble(comm + 8);
            r.sampleRate = static_cast<int>(std::lround(rate));
            haveComm = true;
        } else if (std::memcmp(chunk, "SSND", 4) == 0 && size >= 8) {
            unsigned char ssnd[8];
            if (!readExact(file, ssnd, sizeof ssnd)) throw ProbeError("truncated SSND chunk");
            const std::uint64_t start = payload + 8 + be32(ssnd);
            const std::uint64_t end = std::min<std::uint64_t>(payload + size, fileSize);
            if (start <= end) {
                r.hashOffset = start;
                r.hashLength = end - start;
                haveSsnd = true;
            }
        }
        pos = payload + size + (size & 1u);
    }

    if (!haveComm) throw ProbeError("AIFF file has no COMM chunk");
    if (!haveSsnd) throw ProbeError("AIFF file has no SSND chunk");
    if (rate <= 0.0 || r.channels <= 0) throw ProbeError("AIFF COMM chunk is invalid");
    r.durationSeconds = static_cast<double>(frames) / rate;
    return r;
}

} // namespace asma::detail
