// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace asma {

// XXH3-64 over [offset, offset + length) of the file, as 16 lowercase hex
// characters. Hashes whatever exists if the file is shorter. Throws ProbeError
// when the file cannot be opened or seeked.
std::string contentHash(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length);

} // namespace asma
