// SPDX-License-Identifier: GPL-3.0-only
#include "PluginState.h"

#include "asma/core/Json.h"

namespace asma::app {

namespace {

const char* directionName(audio::Direction d)
{
    switch (d) {
    case audio::Direction::Forward: return "forward";
    case audio::Direction::Reverse: return "reverse";
    case audio::Direction::PingPong: return "pingpong";
    }
    return "forward";
}

const char* loopName(audio::LoopMode m)
{
    switch (m) {
    case audio::LoopMode::Auto: return "auto";
    case audio::LoopMode::On: return "on";
    case audio::LoopMode::Off: return "off";
    }
    return "auto";
}

} // namespace

std::string toJson(const PluginState& s)
{
    return JsonLine()
        .num("v", 1)
        .str("selected", s.selected)
        .str("search", searchModelToJson(s.search))
        .boolean("tempo_sync", s.sync.tempo)
        .boolean("key_sync", s.sync.key)
        .str("project_key", s.sync.projectKey.view())
        .real("manual_bpm", s.sync.hostBpm)
        .boolean("gain_match", s.gainMatch)
        .real("quantise", s.quantise)
        .real("trim_start", s.edits.trimStart)
        .real("trim_end", s.edits.trimEnd)
        .str("direction", directionName(s.edits.direction))
        .str("loop", loopName(s.edits.loop))
        .num("width", s.width)
        .num("height", s.height)
        .build();
}

PluginState pluginStateFromJson(std::string_view json)
{
    PluginState s;
    JsonValue v;
    try {
        v = parseJson(json);
    } catch (const JsonError&) {
        return s;
    }
    if (!v.isObject()) return s;
    const auto text = [&](const char* key) -> const std::string* {
        const JsonValue* f = v.get(key);
        return f ? f->asString() : nullptr;
    };
    const auto number = [&](const char* key) -> std::optional<double> {
        const JsonValue* f = v.get(key);
        return f ? f->asNumber() : std::nullopt;
    };
    const auto flag = [&](const char* key) -> std::optional<bool> {
        const JsonValue* f = v.get(key);
        return f ? f->asBool() : std::nullopt;
    };

    if (const auto* x = text("selected")) s.selected = *x;
    if (const auto* x = text("search"))
        if (auto model = searchModelFromJson(*x)) s.search = std::move(*model);
    if (const auto x = flag("tempo_sync")) s.sync.tempo = *x;
    if (const auto x = flag("key_sync")) s.sync.key = *x;
    if (const auto* x = text("project_key"); x && audio::keyInterval(*x, "C")) s.sync.projectKey = audio::KeyName(*x);
    if (const auto x = number("manual_bpm"); x && *x >= 0.0 && *x <= 999.0) s.sync.hostBpm = *x;
    if (const auto x = flag("gain_match")) s.gainMatch = *x;
    if (const auto x = number("quantise"); x && *x >= 0.0 && *x <= 64.0) s.quantise = *x;
    if (const auto x = number("trim_start"); x && *x >= 0.0) s.edits.trimStart = *x;
    if (const auto x = number("trim_end")) s.edits.trimEnd = *x;
    if (const auto* x = text("direction")) {
        if (*x == "reverse") s.edits.direction = audio::Direction::Reverse;
        else if (*x == "pingpong") s.edits.direction = audio::Direction::PingPong;
    }
    if (const auto* x = text("loop")) {
        if (*x == "on") s.edits.loop = audio::LoopMode::On;
        else if (*x == "off") s.edits.loop = audio::LoopMode::Off;
    }
    if (const auto x = number("width"); x && *x >= 400 && *x <= 8000) s.width = static_cast<int>(*x);
    if (const auto x = number("height"); x && *x >= 300 && *x <= 8000) s.height = static_cast<int>(*x);
    return s;
}

} // namespace asma::app
