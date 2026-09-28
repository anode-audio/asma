// SPDX-License-Identifier: GPL-3.0-only
#include "asma/core/Json.h"

#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>

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
    // snprintf follows LC_NUMERIC, and a host application may have set one
    // with a decimal comma.
    for (char* c = buf; *c; ++c)
        if (*c == ',') *c = '.';
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

JsonLine& JsonLine::strings(std::string_view k, const std::vector<std::string>& values)
{
    key(k);
    body_ += '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) body_ += ',';
        body_ += '"';
        body_ += jsonEscape(values[i]);
        body_ += '"';
    }
    body_ += ']';
    return *this;
}

std::string JsonLine::build() const { return "{" + body_ + "}"; }

std::optional<bool> JsonValue::asBool() const
{
    if (const auto* v = std::get_if<bool>(&value_)) return *v;
    return std::nullopt;
}

std::optional<double> JsonValue::asNumber() const
{
    if (const auto* v = std::get_if<double>(&value_)) return *v;
    return std::nullopt;
}

std::optional<std::int64_t> JsonValue::asInt() const
{
    const auto* v = std::get_if<double>(&value_);
    // 2^63 is exactly representable; anything at or beyond it does not fit.
    if (!v || std::floor(*v) != *v || *v < -9223372036854775808.0 || *v >= 9223372036854775808.0)
        return std::nullopt;
    return static_cast<std::int64_t>(*v);
}

const std::string* JsonValue::asString() const { return std::get_if<std::string>(&value_); }
const JsonValue::Array* JsonValue::asArray() const { return std::get_if<Array>(&value_); }
const JsonValue::Object* JsonValue::asObject() const { return std::get_if<Object>(&value_); }

const JsonValue* JsonValue::get(std::string_view key) const
{
    const auto* object = asObject();
    if (!object) return nullptr;
    for (auto it = object->rbegin(); it != object->rend(); ++it)
        if (it->first == key) return &it->second;
    return nullptr;
}

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    JsonValue document()
    {
        JsonValue value = parseValue(0);
        skipSpace();
        if (pos_ != text_.size()) fail("unexpected text after the value");
        return value;
    }

private:
    static constexpr int kMaxDepth = 64;

    [[noreturn]] void fail(const std::string& what) const
    {
        throw JsonError("invalid JSON at byte " + std::to_string(pos_) + ": " + what);
    }

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return atEnd() ? '\0' : text_[pos_]; }

    void skipSpace()
    {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')) ++pos_;
    }

    void expect(char c)
    {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    void literal(std::string_view word)
    {
        if (text_.substr(pos_, word.size()) != word) fail("unknown literal");
        pos_ += word.size();
    }

    JsonValue parseValue(int depth)
    {
        if (depth > kMaxDepth) fail("nested too deeply");
        skipSpace();
        switch (peek()) {
        case '{': return parseObject(depth);
        case '[': return parseArray(depth);
        case '"': return JsonValue(parseString());
        case 't': literal("true"); return JsonValue(true);
        case 'f': literal("false"); return JsonValue(false);
        case 'n': literal("null"); return JsonValue();
        default: return JsonValue(parseNumber());
        }
    }

    JsonValue parseObject(int depth)
    {
        expect('{');
        JsonValue::Object object;
        skipSpace();
        if (peek() == '}') {
            ++pos_;
            return JsonValue(std::move(object));
        }
        for (;;) {
            skipSpace();
            std::string key = parseString();
            skipSpace();
            expect(':');
            object.emplace_back(std::move(key), parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect('}');
            return JsonValue(std::move(object));
        }
    }

    JsonValue parseArray(int depth)
    {
        expect('[');
        JsonValue::Array array;
        skipSpace();
        if (peek() == ']') {
            ++pos_;
            return JsonValue(std::move(array));
        }
        for (;;) {
            array.push_back(parseValue(depth + 1));
            skipSpace();
            if (peek() == ',') {
                ++pos_;
                continue;
            }
            expect(']');
            return JsonValue(std::move(array));
        }
    }

    unsigned hex4()
    {
        if (pos_ + 4 > text_.size()) fail("short \\u escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad \\u escape");
        }
        return value;
    }

    static void appendUtf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string parseString()
    {
        expect('"');
        std::string out;
        for (;;) {
            if (atEnd()) fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (atEnd()) fail("unterminated string");
            switch (text_[pos_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = hex4();
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (text_.substr(pos_, 2) != "\\u") fail("unpaired surrogate");
                    pos_ += 2;
                    const unsigned low = hex4();
                    if (low < 0xDC00 || low > 0xDFFF) fail("unpaired surrogate");
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    fail("unpaired surrogate");
                }
                appendUtf8(out, cp);
                break;
            }
            default: fail("bad escape");
            }
        }
    }

    double parseNumber()
    {
        const std::size_t start = pos_;
        const auto digits = [&] {
            const std::size_t from = pos_;
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
            return pos_ - from;
        };
        if (peek() == '-') ++pos_;
        if (peek() == '0') ++pos_;
        else if (digits() == 0) fail("expected a value");
        if (peek() == '.') {
            ++pos_;
            if (digits() == 0) fail("expected digits after '.'");
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (digits() == 0) fail("expected exponent digits");
        }
        // A classic-locale stream, because strtod follows LC_NUMERIC.
        std::istringstream in(std::string(text_.substr(start, pos_ - start)));
        in.imbue(std::locale::classic());
        double value = 0.0;
        in >> value;
        if (!std::isfinite(value)) fail("number out of range");
        return value;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

} // namespace

JsonValue parseJson(std::string_view text) { return Parser(text).document(); }

} // namespace asma
