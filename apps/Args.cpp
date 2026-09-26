// SPDX-License-Identifier: GPL-3.0-only
#include "Args.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace asma::cli {

namespace {

#ifdef _WIN32
std::string narrow(const wchar_t* wide)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr);
    return out;
}
#endif

} // namespace

Args Args::fromMain(int argc, char** argv)
{
    std::vector<std::string> args;
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; ++i) args.push_back(narrow(wide[i]));
    LocalFree(wide);
#else
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
#endif
    return Args(std::move(args));
}

bool Args::flag(std::string_view name)
{
    const std::string wanted = "--" + std::string(name);
    bool found = false;
    for (auto it = args_.begin(); it != args_.end();) {
        if (*it == wanted) {
            it = args_.erase(it);
            found = true;
        } else {
            ++it;
        }
    }
    return found;
}

std::vector<std::string> Args::options(std::string_view name)
{
    const std::string wanted = "--" + std::string(name);
    const std::string prefix = wanted + "=";
    std::vector<std::string> values;
    for (auto it = args_.begin(); it != args_.end();) {
        if (*it == wanted) {
            if (it + 1 == args_.end()) throw UsageError("option " + wanted + " needs a value");
            values.push_back(*(it + 1));
            it = args_.erase(it, it + 2);
        } else if (it->rfind(prefix, 0) == 0) {
            values.push_back(it->substr(prefix.size()));
            it = args_.erase(it);
        } else {
            ++it;
        }
    }
    return values;
}

std::optional<std::string> Args::option(std::string_view name)
{
    auto values = options(name);
    if (values.empty()) return std::nullopt;
    return values.back();
}

std::optional<std::string> Args::positional()
{
    for (auto it = args_.begin(); it != args_.end(); ++it) {
        if (it->rfind("--", 0) == 0) continue;
        std::string value = *it;
        args_.erase(it);
        return value;
    }
    return std::nullopt;
}

} // namespace asma::cli
