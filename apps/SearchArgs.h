// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Args.h"

#include "asma/core/Db.h"
#include "asma/core/Query.h"

#include <string>

namespace asma::cli {

void rejectLeftovers(const Args& args);
double toDouble(const std::string& text, const char* what);

// Builds a search model from query options and the remaining words, starting
// from --saved NAME when given. Consumes everything it reads and throws
// UsageError for an unknown option, a bad value or an unknown collection or
// saved search. --limit is read too; the caller decides whether it matters.
SearchModel modelFromArgs(Args& args, Db& db);

} // namespace asma::cli
