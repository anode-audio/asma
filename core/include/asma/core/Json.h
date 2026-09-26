// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace asma {

// Escapes for use inside a JSON string. UTF-8 passes through unchanged.
std::string jsonEscape(std::string_view text);

// Builds one flat JSON object, keys in insertion order. Distinct method names
// avoid overload surprises (a string literal would otherwise pick bool).
class JsonLine {
public:
    JsonLine& str(std::string_view key, std::string_view value);
    JsonLine& num(std::string_view key, std::int64_t value);
    JsonLine& real(std::string_view key, double value); // NaN/inf become null
    JsonLine& boolean(std::string_view key, bool value);
    JsonLine& null(std::string_view key);
    std::string build() const;

private:
    void key(std::string_view name);
    std::string body_;
};

} // namespace asma
