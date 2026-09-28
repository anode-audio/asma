// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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
    JsonLine& strings(std::string_view key, const std::vector<std::string>& values);
    std::string build() const;

private:
    void key(std::string_view name);
    std::string body_;
};

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A parsed JSON value. The typed accessors return nothing on a type mismatch,
// so a reader can skip a bad field instead of failing the whole document.
class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>; // document order

    JsonValue() = default;
    explicit JsonValue(bool value) : value_(value) {}
    explicit JsonValue(double value) : value_(value) {}
    explicit JsonValue(std::string value) : value_(std::move(value)) {}
    explicit JsonValue(Array value) : value_(std::move(value)) {}
    explicit JsonValue(Object value) : value_(std::move(value)) {}

    bool isNull() const { return std::holds_alternative<std::monostate>(value_); }
    bool isObject() const { return std::holds_alternative<Object>(value_); }
    std::optional<bool> asBool() const;
    std::optional<double> asNumber() const;
    std::optional<std::int64_t> asInt() const; // whole numbers that fit only
    const std::string* asString() const;
    const Array* asArray() const;
    const Object* asObject() const;
    // Member of an object (the last one when a key repeats); nullptr when this
    // is not an object or has no such key.
    const JsonValue* get(std::string_view key) const;

private:
    std::variant<std::monostate, bool, double, std::string, Array, Object> value_;
};

// Parses one JSON document (RFC 8259). Surrounding whitespace is allowed,
// anything else after the value is not. Throws JsonError on malformed input,
// including nesting deeper than 64 levels.
JsonValue parseJson(std::string_view text);

} // namespace asma
