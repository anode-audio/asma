// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace asma::cli {

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Minimal argv parser. Options are "--name value" or "--name=value", flags are
// "--name". Each accessor removes what it consumed, so take global options
// first, then the command, then command options, then rest().
class Args {
public:
    // UTF-8 arguments on every platform (Windows reads GetCommandLineW).
    static Args fromMain(int argc, char** argv);
    explicit Args(std::vector<std::string> args) : args_(std::move(args)) {}

    bool flag(std::string_view name);
    std::optional<std::string> option(std::string_view name);   // last occurrence
    std::vector<std::string> options(std::string_view name);    // every occurrence
    std::optional<std::string> positional();                    // first non-option
    std::vector<std::string> rest() const { return args_; }

private:
    std::vector<std::string> args_;
};

} // namespace asma::cli
