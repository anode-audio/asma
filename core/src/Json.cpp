// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Json.h"

#include <cmath>
#include <cstdio>

namespace asma {

std::string jsonEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

void JsonLine::key(std::string_view name)
{
    if (!body_.empty()) body_ += ',';
    body_ += '"';
    body_ += jsonEscape(name);
    body_ += "\":";
}

JsonLine& JsonLine::str(std::string_view k, std::string_view value)
{
    key(k);
    body_ += '"';
    body_ += jsonEscape(value);
    body_ += '"';
    return *this;
}

JsonLine& JsonLine::num(std::string_view k, std::int64_t value)
{
    key(k);
    body_ += std::to_string(value);
    return *this;
}

JsonLine& JsonLine::real(std::string_view k, double value)
{
    key(k);
    if (!std::isfinite(value)) {
        body_ += "null";
        return *this;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.10g", value);
    body_ += buf;
    return *this;
}

JsonLine& JsonLine::boolean(std::string_view k, bool value)
{
    key(k);
    body_ += value ? "true" : "false";
    return *this;
}

JsonLine& JsonLine::null(std::string_view k)
{
    key(k);
    body_ += "null";
    return *this;
}

std::string JsonLine::build() const { return "{" + body_ + "}"; }

} // namespace asma
