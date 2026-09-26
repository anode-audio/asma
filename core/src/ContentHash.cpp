// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/ContentHash.h"

#include "asma/core/AudioProbe.h"
#include "asma/core/Fs.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#include <xxhash.h>

namespace asma {

std::string contentHash(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length)
{
    FilePtr file(openFileRead(path));
    if (!file) throw FileAccessError("cannot open file for hashing");
    if (!seekFile(file.get(), offset)) throw FileAccessError("cannot seek to the audio data");

    std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> state(XXH3_createState(), &XXH3_freeState);
    if (!state || XXH3_64bits_reset(state.get()) != XXH_OK) throw ProbeError("cannot initialise the hash");

    std::vector<unsigned char> buffer(1u << 20);
    std::uint64_t remaining = length;
    while (remaining > 0) {
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        const std::size_t got = std::fread(buffer.data(), 1, want, file.get());
        if (got == 0) break;
        XXH3_64bits_update(state.get(), buffer.data(), got);
        remaining -= got;
    }
    // A short read means the file changed or the drive went away mid-read;
    // storing a hash of part of the data would break re-linking for good.
    if (remaining > 0) throw FileAccessError("file ended before its audio data did");

    char hex[17];
    std::snprintf(hex, sizeof hex, "%016llx",
                  static_cast<unsigned long long>(XXH3_64bits_digest(state.get())));
    return hex;
}

} // namespace asma
