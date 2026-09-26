// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace asma {

// XXH3-64 over [offset, offset + length) of the file, as 16 lowercase hex
// characters. Throws FileAccessError when the file cannot be opened, seeked
// or read to the end of the range.
std::string contentHash(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length);

} // namespace asma
